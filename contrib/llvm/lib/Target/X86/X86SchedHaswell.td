//=- X86SchedHaswell.td - X86 Haswell Scheduling -------------*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Haswell to support instruction
// scheduling and other instruction cost heuristics.
//
// Note that we define some instructions here that are not supported by haswell,
// but we still have to define them because KNL uses the HSW model.
// They are currently tagged with a comment `Unsupported = 1`.
// FIXME: Use Unsupported = 1 once KNL has its own model.
//
//===----------------------------------------------------------------------===//

def HaswellModel : SchedMachineModel {
  // All x86 instructions are modeled as a single micro-op, and HW can decode 4
  // instructions per cycle.
  let IssueWidth = 4;
  let MicroOpBufferSize = 192; // Based on the reorder buffer.
  let LoadLatency = 5;
  let MispredictPenalty = 16;

  // Based on the LSD (loop-stream detector) queue size and benchmarking data.
  let LoopMicroOpBufferSize = 50;

  // This flag is set to allow the scheduler to assign a default model to
  // unrecognized opcodes.
  let CompleteModel = 0;
}

let SchedModel = HaswellModel in {

// Haswell can issue micro-ops to 8 different ports in one cycle.

// Ports 0, 1, 5, and 6 handle all computation.
// Port 4 gets the data half of stores. Store data can be available later than
// the store address, but since we don't model the latency of stores, we can
// ignore that.
// Ports 2 and 3 are identical. They handle loads and the address half of
// stores. Port 7 can handle address calculations.
def HWPort0 : ProcResource<1>;
def HWPort1 : ProcResource<1>;
def HWPort2 : ProcResource<1>;
def HWPort3 : ProcResource<1>;
def HWPort4 : ProcResource<1>;
def HWPort5 : ProcResource<1>;
def HWPort6 : ProcResource<1>;
def HWPort7 : ProcResource<1>;

// Many micro-ops are capable of issuing on multiple ports.
def HWPort01  : ProcResGroup<[HWPort0, HWPort1]>;
def HWPort23  : ProcResGroup<[HWPort2, HWPort3]>;
def HWPort237 : ProcResGroup<[HWPort2, HWPort3, HWPort7]>;
def HWPort04  : ProcResGroup<[HWPort0, HWPort4]>;
def HWPort05  : ProcResGroup<[HWPort0, HWPort5]>;
def HWPort06  : ProcResGroup<[HWPort0, HWPort6]>;
def HWPort15  : ProcResGroup<[HWPort1, HWPort5]>;
def HWPort16  : ProcResGroup<[HWPort1, HWPort6]>;
def HWPort56  : ProcResGroup<[HWPort5, HWPort6]>;
def HWPort015 : ProcResGroup<[HWPort0, HWPort1, HWPort5]>;
def HWPort056 : ProcResGroup<[HWPort0, HWPort5, HWPort6]>;
def HWPort0156: ProcResGroup<[HWPort0, HWPort1, HWPort5, HWPort6]>;

// 60 Entry Unified Scheduler
def HWPortAny : ProcResGroup<[HWPort0, HWPort1, HWPort2, HWPort3, HWPort4,
                              HWPort5, HWPort6, HWPort7]> {
  let BufferSize=60;
}

// Integer division issued on port 0.
def HWDivider : ProcResource<1>;
// FP division and sqrt on port 0.
def HWFPDivider : ProcResource<1>;

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
multiclass HWWriteResPair<X86FoldableSchedWrite SchedRW,
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
  def : WriteRes<SchedRW.Folded, !listconcat([HWPort23], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ResourceCycles = !listconcat([1], Res);
    let NumMicroOps = !add(UOps, 1);
  }
}

// A folded store needs a cycle on port 4 for the store data, and an extra port
// 2/3/7 cycle to recompute the address.
def : WriteRes<WriteRMW, [HWPort237,HWPort4]>;

// Store_addr on 237.
// Store_data on 4.
defm : X86WriteRes<WriteStore,   [HWPort237, HWPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteStoreNT, [HWPort237, HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteLoad,    [HWPort23], 5, [1], 1>;
defm : X86WriteRes<WriteMove,    [HWPort0156], 1, [1], 1>;
def  : WriteRes<WriteZero,       []>;

// Arithmetic.
defm : HWWriteResPair<WriteALU,    [HWPort0156], 1>;
defm : HWWriteResPair<WriteADC,    [HWPort06, HWPort0156], 2, [1,1], 2>;

// Integer multiplication.
defm : HWWriteResPair<WriteIMul8,     [HWPort1],   3>;
defm : HWWriteResPair<WriteIMul16,    [HWPort1,HWPort06,HWPort0156], 4, [1,1,2], 4>;
defm : X86WriteRes<WriteIMul16Imm,    [HWPort1,HWPort0156], 4, [1,1], 2>;
defm : X86WriteRes<WriteIMul16ImmLd,  [HWPort1,HWPort0156,HWPort23], 8, [1,1,1], 3>;
defm : HWWriteResPair<WriteIMul16Reg, [HWPort1],   3>;
defm : HWWriteResPair<WriteIMul32,    [HWPort1,HWPort06,HWPort0156], 4, [1,1,1], 3>;
defm : HWWriteResPair<WriteIMul32Imm, [HWPort1],   3>;
defm : HWWriteResPair<WriteIMul32Reg, [HWPort1],   3>;
defm : HWWriteResPair<WriteIMul64,    [HWPort1,HWPort6], 4, [1,1], 2>;
defm : HWWriteResPair<WriteIMul64Imm, [HWPort1],   3>;
defm : HWWriteResPair<WriteIMul64Reg, [HWPort1],   3>;
def  : WriteRes<WriteIMulH, []> { let Latency = 3; }

defm : X86WriteRes<WriteBSWAP32,   [HWPort15], 1, [1], 1>;
defm : X86WriteRes<WriteBSWAP64,   [HWPort06, HWPort15], 2, [1,1], 2>;
defm : X86WriteRes<WriteCMPXCHG,[HWPort06, HWPort0156], 5, [2,3], 5>;
defm : X86WriteRes<WriteCMPXCHGRMW,[HWPort23,HWPort06,HWPort0156,HWPort237,HWPort4], 9, [1,2,1,1,1], 6>;
defm : X86WriteRes<WriteXCHG, [HWPort0156], 2, [3], 3>;

// Integer shifts and rotates.
defm : HWWriteResPair<WriteShift,    [HWPort06],  1>;
defm : HWWriteResPair<WriteShiftCL,  [HWPort06, HWPort0156],  3, [2,1], 3>;
defm : HWWriteResPair<WriteRotate,   [HWPort06],  2, [2], 2>;
defm : HWWriteResPair<WriteRotateCL, [HWPort06, HWPort0156],  3, [2,1], 3>;

// SHLD/SHRD.
defm : X86WriteRes<WriteSHDrri, [HWPort1], 3, [1], 1>;
defm : X86WriteRes<WriteSHDrrcl,[HWPort1, HWPort06, HWPort0156], 6, [1, 1, 2], 4>;
defm : X86WriteRes<WriteSHDmri, [HWPort1, HWPort23, HWPort237, HWPort0156], 10, [1, 1, 1, 1], 4>;
defm : X86WriteRes<WriteSHDmrcl,[HWPort1, HWPort23, HWPort237, HWPort06, HWPort0156], 12, [1, 1, 1, 1, 2], 6>;

defm : HWWriteResPair<WriteJump,   [HWPort06],  1>;
defm : HWWriteResPair<WriteCRC32,  [HWPort1],   3>;

defm : HWWriteResPair<WriteCMOV,  [HWPort06,HWPort0156], 2, [1,1], 2>; // Conditional move.
defm : HWWriteResPair<WriteCMOV2, [HWPort06,HWPort0156], 3, [1,2], 3>; // Conditional (CF + ZF flag) move.
defm : X86WriteRes<WriteFCMOV, [HWPort1], 3, [1], 1>; // x87 conditional move.
def  : WriteRes<WriteSETCC, [HWPort06]>; // Setcc.
def  : WriteRes<WriteSETCCStore, [HWPort06,HWPort4,HWPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

defm : X86WriteRes<WriteLAHFSAHF,        [HWPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTest,         [HWPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [HWPort06,HWPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteBitTestRegLd,    [], 1, [], 10>;
defm : X86WriteRes<WriteBitTestSet,      [HWPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestSetImmLd, [HWPort06,HWPort23], 6, [1,1], 3>;
//defm : X86WriteRes<WriteBitTestSetRegLd, [], 1, [], 11>;

// This is for simple LEAs with one or two input operands.
// The complex ones can only execute on port 1, and they require two cycles on
// the port to read all inputs. We don't model that.
def : WriteRes<WriteLEA, [HWPort15]>;

// Bit counts.
defm : HWWriteResPair<WriteBSF, [HWPort1], 3>;
defm : HWWriteResPair<WriteBSR, [HWPort1], 3>;
defm : HWWriteResPair<WriteLZCNT,          [HWPort1], 3>;
defm : HWWriteResPair<WriteTZCNT,          [HWPort1], 3>;
defm : HWWriteResPair<WritePOPCNT,         [HWPort1], 3>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : HWWriteResPair<WriteBEXTR, [HWPort06,HWPort15], 2, [1,1], 2>;
defm : HWWriteResPair<WriteBLS,   [HWPort15], 1>;
defm : HWWriteResPair<WriteBZHI,  [HWPort15], 1>;

// TODO: Why isn't the HWDivider used?
defm : X86WriteRes<WriteDiv8,     [HWPort0,HWPort1,HWPort5,HWPort6], 22, [], 9>;
defm : X86WriteRes<WriteDiv16,    [HWPort0,HWPort1,HWPort5,HWPort6,HWPort01,HWPort0156], 98, [7,7,3,3,1,11], 32>;
defm : X86WriteRes<WriteDiv32,    [HWPort0,HWPort1,HWPort5,HWPort6,HWPort01,HWPort0156], 98, [7,7,3,3,1,11], 32>;
defm : X86WriteRes<WriteDiv64,    [HWPort0,HWPort1,HWPort5,HWPort6,HWPort01,HWPort0156], 98, [7,7,3,3,1,11], 32>;
defm : X86WriteRes<WriteDiv8Ld,   [HWPort0,HWPort23,HWDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv16Ld,  [HWPort0,HWPort23,HWDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv32Ld,  [HWPort0,HWPort23,HWDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv64Ld,  [HWPort0,HWPort23,HWDivider], 29, [1,1,10], 2>;

defm : X86WriteRes<WriteIDiv8,    [HWPort0,HWPort1,HWPort5,HWPort6], 23, [], 9>;
defm : X86WriteRes<WriteIDiv16,   [HWPort0,HWPort1,HWPort5,HWPort6,HWPort06,HWPort0156], 112, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv32,   [HWPort0,HWPort1,HWPort5,HWPort6,HWPort06,HWPort0156], 112, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv64,   [HWPort0,HWPort1,HWPort5,HWPort6,HWPort06,HWPort0156], 112, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv8Ld,  [HWPort0,HWPort23,HWDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteIDiv16Ld, [HWPort0,HWPort23,HWDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteIDiv32Ld, [HWPort0,HWPort23,HWDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteIDiv64Ld, [HWPort0,HWPort23,HWDivider], 29, [1,1,10], 2>;

// Scalar and vector floating point.
defm : X86WriteRes<WriteFLD0,          [HWPort01], 1, [1], 1>;
defm : X86WriteRes<WriteFLD1,          [HWPort01], 1, [2], 2>;
defm : X86WriteRes<WriteFLDC,          [HWPort01], 1, [2], 2>;
defm : X86WriteRes<WriteFLoad,         [HWPort23], 5, [1], 1>;
defm : X86WriteRes<WriteFLoadX,        [HWPort23], 6, [1], 1>;
defm : X86WriteRes<WriteFLoadY,        [HWPort23], 7, [1], 1>;
defm : X86WriteRes<WriteFMaskedLoad,   [HWPort23,HWPort5], 8, [1,2], 3>;
defm : X86WriteRes<WriteFMaskedLoadY,  [HWPort23,HWPort5], 9, [1,2], 3>;
defm : X86WriteRes<WriteFStore,        [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreX,       [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreY,       [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNT,      [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNTX,     [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNTY,     [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFMaskedStore,  [HWPort0,HWPort4,HWPort237,HWPort15], 5, [1,1,1,1], 4>;
defm : X86WriteRes<WriteFMaskedStoreY, [HWPort0,HWPort4,HWPort237,HWPort15], 5, [1,1,1,1], 4>;
defm : X86WriteRes<WriteFMove,         [HWPort5], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveX,        [HWPort5], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveY,        [HWPort5], 1, [1], 1>;
defm : X86WriteRes<WriteEMMS,          [HWPort01,HWPort15,HWPort015,HWPort0156], 31, [8,1,21,1], 31>;

defm : HWWriteResPair<WriteFAdd,    [HWPort1],  3, [1], 1, 5>;
defm : HWWriteResPair<WriteFAddX,   [HWPort1],  3, [1], 1, 6>;
defm : HWWriteResPair<WriteFAddY,   [HWPort1],  3, [1], 1, 7>;
defm : HWWriteResPair<WriteFAddZ,   [HWPort1],  3, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFAdd64,  [HWPort1],  3, [1], 1, 5>;
defm : HWWriteResPair<WriteFAdd64X, [HWPort1],  3, [1], 1, 6>;
defm : HWWriteResPair<WriteFAdd64Y, [HWPort1],  3, [1], 1, 7>;
defm : HWWriteResPair<WriteFAdd64Z, [HWPort1],  3, [1], 1, 7>; // Unsupported = 1

defm : HWWriteResPair<WriteFCmp,    [HWPort1],  3, [1], 1, 5>;
defm : HWWriteResPair<WriteFCmpX,   [HWPort1],  3, [1], 1, 6>;
defm : HWWriteResPair<WriteFCmpY,   [HWPort1],  3, [1], 1, 7>;
defm : HWWriteResPair<WriteFCmpZ,   [HWPort1],  3, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFCmp64,  [HWPort1],  3, [1], 1, 5>;
defm : HWWriteResPair<WriteFCmp64X, [HWPort1],  3, [1], 1, 6>;
defm : HWWriteResPair<WriteFCmp64Y, [HWPort1],  3, [1], 1, 7>;
defm : HWWriteResPair<WriteFCmp64Z, [HWPort1],  3, [1], 1, 7>; // Unsupported = 1

defm : HWWriteResPair<WriteFCom,    [HWPort1],  3>;

defm : HWWriteResPair<WriteFMul,    [HWPort01],  5, [1], 1, 5>;
defm : HWWriteResPair<WriteFMulX,   [HWPort01],  5, [1], 1, 6>;
defm : HWWriteResPair<WriteFMulY,   [HWPort01],  5, [1], 1, 7>;
defm : HWWriteResPair<WriteFMulZ,   [HWPort01],  5, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFMul64,  [HWPort01],  5, [1], 1, 5>;
defm : HWWriteResPair<WriteFMul64X, [HWPort01],  5, [1], 1, 6>;
defm : HWWriteResPair<WriteFMul64Y, [HWPort01],  5, [1], 1, 7>;
defm : HWWriteResPair<WriteFMul64Z, [HWPort01],  5, [1], 1, 7>; // Unsupported = 1

defm : HWWriteResPair<WriteFDiv,    [HWPort0,HWFPDivider], 13, [1,7], 1, 5>;
defm : HWWriteResPair<WriteFDivX,   [HWPort0,HWFPDivider], 13, [1,7], 1, 6>;
defm : HWWriteResPair<WriteFDivY,   [HWPort0,HWPort15,HWFPDivider], 21, [2,1,14], 3, 7>;
defm : HWWriteResPair<WriteFDivZ,   [HWPort0,HWPort15,HWFPDivider], 21, [2,1,14], 3, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFDiv64,  [HWPort0,HWFPDivider], 20, [1,14], 1, 5>;
defm : HWWriteResPair<WriteFDiv64X, [HWPort0,HWFPDivider], 20, [1,14], 1, 6>;
defm : HWWriteResPair<WriteFDiv64Y, [HWPort0,HWPort15,HWFPDivider], 35, [2,1,28], 3, 7>;
defm : HWWriteResPair<WriteFDiv64Z, [HWPort0,HWPort15,HWFPDivider], 35, [2,1,28], 3, 7>; // Unsupported = 1

defm : HWWriteResPair<WriteFRcp,   [HWPort0],  5, [1], 1, 5>;
defm : HWWriteResPair<WriteFRcpX,  [HWPort0],  5, [1], 1, 6>;
defm : HWWriteResPair<WriteFRcpY,  [HWPort0,HWPort015], 11, [2,1], 3, 7>;
defm : HWWriteResPair<WriteFRcpZ,  [HWPort0,HWPort015], 11, [2,1], 3, 7>; // Unsupported = 1

defm : HWWriteResPair<WriteFRsqrt, [HWPort0],  5, [1], 1, 5>;
defm : HWWriteResPair<WriteFRsqrtX,[HWPort0],  5, [1], 1, 6>;
defm : HWWriteResPair<WriteFRsqrtY,[HWPort0,HWPort015], 11, [2,1], 3, 7>;
defm : HWWriteResPair<WriteFRsqrtZ,[HWPort0,HWPort015], 11, [2,1], 3, 7>; // Unsupported = 1

defm : HWWriteResPair<WriteFSqrt,    [HWPort0,HWFPDivider], 11, [1,7], 1, 5>;
defm : HWWriteResPair<WriteFSqrtX,   [HWPort0,HWFPDivider], 11, [1,7], 1, 6>;
defm : HWWriteResPair<WriteFSqrtY,   [HWPort0,HWPort15,HWFPDivider], 21, [2,1,14], 3, 7>;
defm : HWWriteResPair<WriteFSqrtZ,   [HWPort0,HWPort15,HWFPDivider], 21, [2,1,14], 3, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFSqrt64,  [HWPort0,HWFPDivider], 16, [1,14], 1, 5>;
defm : HWWriteResPair<WriteFSqrt64X, [HWPort0,HWFPDivider], 16, [1,14], 1, 6>;
defm : HWWriteResPair<WriteFSqrt64Y, [HWPort0,HWPort15,HWFPDivider], 35, [2,1,28], 3, 7>;
defm : HWWriteResPair<WriteFSqrt64Z, [HWPort0,HWPort15,HWFPDivider], 35, [2,1,28], 3, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFSqrt80,  [HWPort0,HWFPDivider], 23, [1,17]>;

defm : HWWriteResPair<WriteFMA,   [HWPort01], 5, [1], 1, 5>;
defm : HWWriteResPair<WriteFMAX,  [HWPort01], 5, [1], 1, 6>;
defm : HWWriteResPair<WriteFMAY,  [HWPort01], 5, [1], 1, 7>;
defm : HWWriteResPair<WriteFMAZ,  [HWPort01], 5, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteDPPD,  [HWPort0,HWPort1,HWPort5],  9, [1,1,1], 3, 6>;
defm : HWWriteResPair<WriteDPPS,  [HWPort0,HWPort1,HWPort5], 14, [2,1,1], 4, 6>;
defm : HWWriteResPair<WriteDPPSY, [HWPort0,HWPort1,HWPort5], 14, [2,1,1], 4, 7>;
defm : HWWriteResPair<WriteDPPSZ, [HWPort0,HWPort1,HWPort5], 14, [2,1,1], 4, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFSign,  [HWPort0], 1>;
defm : X86WriteRes<WriteFRnd,            [HWPort23],  6, [1],   1>;
defm : X86WriteRes<WriteFRndY,           [HWPort23],  6, [1],   1>;
defm : X86WriteRes<WriteFRndZ,           [HWPort23],  6, [1],   1>; // Unsupported = 1
defm : X86WriteRes<WriteFRndLd,  [HWPort1,HWPort23], 12, [2,1], 3>;
defm : X86WriteRes<WriteFRndYLd, [HWPort1,HWPort23], 13, [2,1], 3>;
defm : X86WriteRes<WriteFRndZLd, [HWPort1,HWPort23], 13, [2,1], 3>; // Unsupported = 1
defm : HWWriteResPair<WriteFLogic,  [HWPort5], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteFLogicY, [HWPort5], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteFLogicZ, [HWPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFTest,   [HWPort0], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteFTestY,  [HWPort0], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteFTestZ,  [HWPort0], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFShuffle,  [HWPort5], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteFShuffleY, [HWPort5], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteFShuffleZ, [HWPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFVarShuffle,  [HWPort5], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteFVarShuffleY, [HWPort5], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteFVarShuffleZ, [HWPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFBlend,  [HWPort015], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteFBlendY, [HWPort015], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteFBlendZ, [HWPort015], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteFShuffle256, [HWPort5], 3, [1], 1, 7>;
defm : HWWriteResPair<WriteFVarShuffle256, [HWPort5], 3, [1], 1, 7>;
defm : HWWriteResPair<WriteFVarBlend,  [HWPort5], 2, [2], 2, 6>;
defm : HWWriteResPair<WriteFVarBlendY, [HWPort5], 2, [2], 2, 7>;
defm : HWWriteResPair<WriteFVarBlendZ, [HWPort5], 2, [2], 2, 7>; // Unsupported = 1

// Conversion between integer and float.
defm : HWWriteResPair<WriteCvtSD2I,   [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPD2I,   [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPD2IY,  [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPD2IZ,  [HWPort1], 3>; // Unsupported = 1
defm : HWWriteResPair<WriteCvtSS2I,   [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPS2I,   [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPS2IY,  [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPS2IZ,  [HWPort1], 3>; // Unsupported = 1

defm : HWWriteResPair<WriteCvtI2SD,   [HWPort1], 4>;
defm : HWWriteResPair<WriteCvtI2PD,   [HWPort1], 4>;
defm : HWWriteResPair<WriteCvtI2PDY,  [HWPort1], 4>;
defm : HWWriteResPair<WriteCvtI2PDZ,  [HWPort1], 4>; // Unsupported = 1
defm : HWWriteResPair<WriteCvtI2SS,   [HWPort1], 4>;
defm : HWWriteResPair<WriteCvtI2PS,   [HWPort1], 4>;
defm : HWWriteResPair<WriteCvtI2PSY,  [HWPort1], 4>;
defm : HWWriteResPair<WriteCvtI2PSZ,  [HWPort1], 4>; // Unsupported = 1

defm : HWWriteResPair<WriteCvtSS2SD,  [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPS2PD,  [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPS2PDY, [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPS2PDZ, [HWPort1], 3>; // Unsupported = 1
defm : HWWriteResPair<WriteCvtSD2SS,  [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPD2PS,  [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPD2PSY, [HWPort1], 3>;
defm : HWWriteResPair<WriteCvtPD2PSZ, [HWPort1], 3>; // Unsupported = 1

defm : X86WriteRes<WriteCvtPH2PS,     [HWPort0,HWPort5], 2, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSY,    [HWPort0,HWPort5], 2, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSZ,    [HWPort0,HWPort5], 2, [1,1], 2>; // Unsupported = 1
defm : X86WriteRes<WriteCvtPH2PSLd,  [HWPort0,HWPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSYLd, [HWPort0,HWPort23], 7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSZLd, [HWPort0,HWPort23], 7, [1,1], 2>; // Unsupported = 1

defm : X86WriteRes<WriteCvtPS2PH,    [HWPort1,HWPort5], 4, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHY,   [HWPort1,HWPort5], 6, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHZ,   [HWPort1,HWPort5], 6, [1,1], 2>; // Unsupported = 1
defm : X86WriteRes<WriteCvtPS2PHSt,  [HWPort1,HWPort4,HWPort5,HWPort237], 5, [1,1,1,1], 4>;
defm : X86WriteRes<WriteCvtPS2PHYSt, [HWPort1,HWPort4,HWPort5,HWPort237], 7, [1,1,1,1], 4>;
defm : X86WriteRes<WriteCvtPS2PHZSt, [HWPort1,HWPort4,HWPort5,HWPort237], 7, [1,1,1,1], 4>; // Unsupported = 1

// Vector integer operations.
defm : X86WriteRes<WriteVecLoad,         [HWPort23], 5, [1], 1>;
defm : X86WriteRes<WriteVecLoadX,        [HWPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadY,        [HWPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecLoadNT,       [HWPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadNTY,      [HWPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecMaskedLoad,   [HWPort23,HWPort5], 8, [1,2], 3>;
defm : X86WriteRes<WriteVecMaskedLoadY,  [HWPort23,HWPort5], 9, [1,2], 3>;
defm : X86WriteRes<WriteVecStore,        [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreX,       [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreY,       [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreNT,      [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreNTY,     [HWPort237,HWPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore,  [HWPort0,HWPort4,HWPort237,HWPort15], 5, [1,1,1,1], 4>;
defm : X86WriteRes<WriteVecMaskedStoreY, [HWPort0,HWPort4,HWPort237,HWPort15], 5, [1,1,1,1], 4>;
defm : X86WriteRes<WriteVecMove,         [HWPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveX,        [HWPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveY,        [HWPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveToGpr,    [HWPort0], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr,  [HWPort5], 1, [1], 1>;

defm : HWWriteResPair<WriteVecLogic, [HWPort015], 1, [1], 1, 5>;
defm : HWWriteResPair<WriteVecLogicX,[HWPort015], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteVecLogicY,[HWPort015], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteVecLogicZ,[HWPort015], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteVecTest,  [HWPort0,HWPort5], 2, [1,1], 2, 6>;
defm : HWWriteResPair<WriteVecTestY, [HWPort0,HWPort5], 4, [1,1], 2, 7>;
defm : HWWriteResPair<WriteVecTestZ, [HWPort0,HWPort5], 4, [1,1], 2, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteVecALU,   [HWPort15],  1, [1], 1, 5>;
defm : HWWriteResPair<WriteVecALUX,  [HWPort15],  1, [1], 1, 6>;
defm : HWWriteResPair<WriteVecALUY,  [HWPort15],  1, [1], 1, 7>;
defm : HWWriteResPair<WriteVecALUZ,  [HWPort15],  1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteVecIMul,  [HWPort0],  5, [1], 1, 5>;
defm : HWWriteResPair<WriteVecIMulX, [HWPort0],  5, [1], 1, 6>;
defm : HWWriteResPair<WriteVecIMulY, [HWPort0],  5, [1], 1, 7>;
defm : HWWriteResPair<WriteVecIMulZ, [HWPort0],  5, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WritePMULLD,   [HWPort0], 10, [2], 2, 6>;
defm : HWWriteResPair<WritePMULLDY,  [HWPort0], 10, [2], 2, 7>;
defm : HWWriteResPair<WritePMULLDZ,  [HWPort0], 10, [2], 2, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteShuffle,  [HWPort5],  1, [1], 1, 5>;
defm : HWWriteResPair<WriteShuffleX, [HWPort5],  1, [1], 1, 6>;
defm : HWWriteResPair<WriteShuffleY, [HWPort5],  1, [1], 1, 7>;
defm : HWWriteResPair<WriteShuffleZ, [HWPort5],  1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteVarShuffle, [HWPort5], 1, [1], 1, 5>;
defm : HWWriteResPair<WriteVarShuffleX,[HWPort5], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteVarShuffleY,[HWPort5], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteVarShuffleZ,[HWPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteBlend,  [HWPort5], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteBlendY, [HWPort5], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteBlendZ, [HWPort5], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteShuffle256, [HWPort5], 3, [1], 1, 7>;
defm : HWWriteResPair<WriteVarShuffle256, [HWPort5], 3, [1], 1, 7>;
defm : HWWriteResPair<WriteVarBlend,  [HWPort5], 2, [2], 2, 6>;
defm : HWWriteResPair<WriteVarBlendY, [HWPort5], 2, [2], 2, 7>;
defm : HWWriteResPair<WriteVarBlendZ, [HWPort5], 2, [2], 2, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteMPSAD,  [HWPort0, HWPort5], 7, [1, 2], 3, 6>;
defm : HWWriteResPair<WriteMPSADY, [HWPort0, HWPort5], 7, [1, 2], 3, 7>;
defm : HWWriteResPair<WriteMPSADZ, [HWPort0, HWPort5], 7, [1, 2], 3, 7>; // Unsupported = 1
defm : HWWriteResPair<WritePSADBW,  [HWPort0], 5, [1], 1, 5>;
defm : HWWriteResPair<WritePSADBWX, [HWPort0], 5, [1], 1, 6>;
defm : HWWriteResPair<WritePSADBWY, [HWPort0], 5, [1], 1, 7>;
defm : HWWriteResPair<WritePSADBWZ, [HWPort0], 5, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WritePHMINPOS, [HWPort0],  5, [1], 1, 6>;

// Vector integer shifts.
defm : HWWriteResPair<WriteVecShift,     [HWPort0], 1, [1], 1, 5>;
defm : HWWriteResPair<WriteVecShiftX,    [HWPort0,HWPort5],  2, [1,1], 2, 6>;
defm : X86WriteRes<WriteVecShiftY,       [HWPort0,HWPort5],  4, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftZ,       [HWPort0,HWPort5],  4, [1,1], 2>; // Unsupported = 1
defm : X86WriteRes<WriteVecShiftYLd,     [HWPort0,HWPort23], 8, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftZLd,     [HWPort0,HWPort23], 8, [1,1], 2>; // Unsupported = 1

defm : HWWriteResPair<WriteVecShiftImm,  [HWPort0], 1, [1], 1, 5>;
defm : HWWriteResPair<WriteVecShiftImmX, [HWPort0], 1, [1], 1, 6>;
defm : HWWriteResPair<WriteVecShiftImmY, [HWPort0], 1, [1], 1, 7>;
defm : HWWriteResPair<WriteVecShiftImmZ, [HWPort0], 1, [1], 1, 7>; // Unsupported = 1
defm : HWWriteResPair<WriteVarVecShift,  [HWPort0, HWPort5], 3, [2,1], 3, 6>;
defm : HWWriteResPair<WriteVarVecShiftY, [HWPort0, HWPort5], 3, [2,1], 3, 7>;
defm : HWWriteResPair<WriteVarVecShiftZ, [HWPort0, HWPort5], 3, [2,1], 3, 7>; // Unsupported = 1

// Vector insert/extract operations.
def : WriteRes<WriteVecInsert, [HWPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def : WriteRes<WriteVecInsertLd, [HWPort5,HWPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def: InstRW<[WriteVecInsertLd], (instregex "(V?)MOV(H|L)(PD|PS)rm")>;

def : WriteRes<WriteVecExtract, [HWPort0,HWPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def : WriteRes<WriteVecExtractSt, [HWPort4,HWPort5,HWPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

// String instructions.

// Packed Compare Implicit Length Strings, Return Mask
def : WriteRes<WritePCmpIStrM, [HWPort0]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def : WriteRes<WritePCmpIStrMLd, [HWPort0, HWPort23]> {
  let Latency = 17;
  let NumMicroOps = 4;
  let ResourceCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Mask
def : WriteRes<WritePCmpEStrM, [HWPort0, HWPort5, HWPort015, HWPort0156]> {
  let Latency = 19;
  let NumMicroOps = 9;
  let ResourceCycles = [4,3,1,1];
}
def : WriteRes<WritePCmpEStrMLd, [HWPort0, HWPort5, HWPort23, HWPort015, HWPort0156]> {
  let Latency = 25;
  let NumMicroOps = 10;
  let ResourceCycles = [4,3,1,1,1];
}

// Packed Compare Implicit Length Strings, Return Index
def : WriteRes<WritePCmpIStrI, [HWPort0]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def : WriteRes<WritePCmpIStrILd, [HWPort0, HWPort23]> {
  let Latency = 17;
  let NumMicroOps = 4;
  let ResourceCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Index
def : WriteRes<WritePCmpEStrI, [HWPort0, HWPort5, HWPort0156]> {
  let Latency = 18;
  let NumMicroOps = 8;
  let ResourceCycles = [4,3,1];
}
def : WriteRes<WritePCmpEStrILd, [HWPort0, HWPort5, HWPort23, HWPort0156]> {
  let Latency = 24;
  let NumMicroOps = 9;
  let ResourceCycles = [4,3,1,1];
}

// MOVMSK Instructions.
def : WriteRes<WriteFMOVMSK,    [HWPort0]> { let Latency = 3; }
def : WriteRes<WriteVecMOVMSK,  [HWPort0]> { let Latency = 3; }
def : WriteRes<WriteVecMOVMSKY, [HWPort0]> { let Latency = 3; }
def : WriteRes<WriteMMXMOVMSK,  [HWPort0]> { let Latency = 1; }

// AES Instructions.
def : WriteRes<WriteAESDecEnc, [HWPort5]> {
  let Latency = 7;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def : WriteRes<WriteAESDecEncLd, [HWPort5, HWPort23]> {
  let Latency = 13;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}

def : WriteRes<WriteAESIMC, [HWPort5]> {
  let Latency = 14;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def : WriteRes<WriteAESIMCLd, [HWPort5, HWPort23]> {
  let Latency = 20;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}

def : WriteRes<WriteAESKeyGen, [HWPort0,HWPort5,HWPort015]> {
  let Latency = 29;
  let NumMicroOps = 11;
  let ResourceCycles = [2,7,2];
}
def : WriteRes<WriteAESKeyGenLd, [HWPort0,HWPort5,HWPort23,HWPort015]> {
  let Latency = 34;
  let NumMicroOps = 11;
  let ResourceCycles = [2,7,1,1];
}

// Carry-less multiplication instructions.
def : WriteRes<WriteCLMul, [HWPort0, HWPort5]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def : WriteRes<WriteCLMulLd, [HWPort0, HWPort5, HWPort23]> {
  let Latency = 17;
  let NumMicroOps = 4;
  let ResourceCycles = [2,1,1];
}

// Load/store MXCSR.
def : WriteRes<WriteLDMXCSR, [HWPort0,HWPort23,HWPort0156]> { let Latency = 7; let NumMicroOps = 3; let ResourceCycles = [1,1,1]; }
def : WriteRes<WriteSTMXCSR, [HWPort4,HWPort5,HWPort237]> { let Latency = 2; let NumMicroOps = 3; let ResourceCycles = [1,1,1]; }

def : WriteRes<WriteSystem,     [HWPort0156]> { let Latency = 100; }
def : WriteRes<WriteMicrocoded, [HWPort0156]> { let Latency = 100; }
def : WriteRes<WriteFence,  [HWPort23, HWPort4]>;
def : WriteRes<WriteNop, []>;

//================ Exceptions ================//

//-- Specific Scheduling Models --//

// Starting with P0.
def HWWriteP0 : SchedWriteRes<[HWPort0]>;

def HWWriteP01 : SchedWriteRes<[HWPort01]>;

def HWWrite2P01 : SchedWriteRes<[HWPort01]> {
  let NumMicroOps = 2;
}
def HWWrite3P01 : SchedWriteRes<[HWPort01]> {
  let NumMicroOps = 3;
}

def HWWriteP0156_P23 : SchedWriteRes<[HWPort0156, HWPort23]> {
  let NumMicroOps = 2;
}

def HWWrite2P0156_P23 : SchedWriteRes<[HWPort0156, HWPort23]> {
  let NumMicroOps = 3;
  let ResourceCycles = [2, 1];
}

// Starting with P1.
def HWWriteP1 : SchedWriteRes<[HWPort1]>;


def HWWrite2P1 : SchedWriteRes<[HWPort1]> {
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}

// Notation:
// - r: register.
// - mm: 64 bit mmx register.
// - x = 128 bit xmm register.
// - (x)mm = mmx or xmm register.
// - y = 256 bit ymm register.
// - v = any vector register.
// - m = memory.

//=== Integer Instructions ===//
//-- Move instructions --//

// XLAT.
def HWWriteXLAT : SchedWriteRes<[]> {
  let Latency = 7;
  let NumMicroOps = 3;
}
def : InstRW<[HWWriteXLAT], (instrs XLAT)>;

// PUSHA.
def HWWritePushA : SchedWriteRes<[]> {
  let NumMicroOps = 19;
}
def : InstRW<[HWWritePushA], (instregex "PUSHA(16|32)")>;

// POPA.
def HWWritePopA : SchedWriteRes<[]> {
  let NumMicroOps = 18;
}
def : InstRW<[HWWritePopA], (instregex "POPA(16|32)")>;

//-- Arithmetic instructions --//

// BTR BTS BTC.
// m,r.
def HWWriteBTRSCmr : SchedWriteRes<[]> {
  let NumMicroOps = 11;
}
def : SchedAlias<WriteBitTestSetRegRMW, HWWriteBTRSCmr>;

//-- Control transfer instructions --//

// CALL.
// i.
def HWWriteRETI : SchedWriteRes<[HWPort23, HWPort6, HWPort015]> {
  let NumMicroOps = 4;
  let ResourceCycles = [1, 2, 1];
}
def : InstRW<[HWWriteRETI], (instregex "RETI(L|Q|W)", "LRETI(L|Q|W)")>;

// BOUND.
// r,m.
def HWWriteBOUND : SchedWriteRes<[]> {
  let NumMicroOps = 15;
}
def : InstRW<[HWWriteBOUND], (instregex "BOUNDS(16|32)rm")>;

// INTO.
def HWWriteINTO : SchedWriteRes<[]> {
  let NumMicroOps = 4;
}
def : InstRW<[HWWriteINTO], (instrs INTO)>;

//-- String instructions --//

// LODSB/W.
def : InstRW<[HWWrite2P0156_P23], (instregex "LODS(B|W)")>;

// LODSD/Q.
def : InstRW<[HWWriteP0156_P23], (instregex "LODS(L|Q)")>;

// MOVS.
def HWWriteMOVS : SchedWriteRes<[HWPort23, HWPort4, HWPort0156]> {
  let Latency = 4;
  let NumMicroOps = 5;
  let ResourceCycles = [2, 1, 2];
}
def : InstRW<[HWWriteMOVS], (instrs MOVSB, MOVSL, MOVSQ, MOVSW)>;

// CMPS.
def HWWriteCMPS : SchedWriteRes<[HWPort23, HWPort0156]> {
  let Latency = 4;
  let NumMicroOps = 5;
  let ResourceCycles = [2, 3];
}
def : InstRW<[HWWriteCMPS], (instregex "CMPS(B|L|Q|W)")>;

//-- Other --//

// RDPMC.f
def HWWriteRDPMC : SchedWriteRes<[]> {
  let NumMicroOps = 34;
}
def : InstRW<[HWWriteRDPMC], (instrs RDPMC)>;

// RDRAND.
def HWWriteRDRAND : SchedWriteRes<[HWPort23, HWPort015]> {
  let NumMicroOps = 17;
  let ResourceCycles = [1, 16];
}
def : InstRW<[HWWriteRDRAND], (instrs RDRAND16r, RDRAND32r, RDRAND64r)>;

//=== Floating Point x87 Instructions ===//
//-- Move instructions --//

// FLD.
// m80.
def : InstRW<[HWWriteP01], (instrs LD_Frr)>;

// FBLD.
// m80.
def HWWriteFBLD : SchedWriteRes<[]> {
  let Latency = 47;
  let NumMicroOps = 43;
}
def : InstRW<[HWWriteFBLD], (instrs FBLDm)>;

// FST(P).
// r.
def : InstRW<[HWWriteP01], (instregex "ST_(F|FP)rr")>;

// FFREE.
def : InstRW<[HWWriteP01], (instregex "FFREE")>;

// FNSAVE.
def HWWriteFNSAVE : SchedWriteRes<[]> {
  let NumMicroOps = 147;
}
def : InstRW<[HWWriteFNSAVE], (instrs FSAVEm)>;

// FRSTOR.
def HWWriteFRSTOR : SchedWriteRes<[]> {
  let NumMicroOps = 90;
}
def : InstRW<[HWWriteFRSTOR], (instrs FRSTORm)>;

//-- Arithmetic instructions --//

// FCOMPP FUCOMPP.
// r.
def : InstRW<[HWWrite2P01], (instrs FCOMPP, UCOM_FPPr)>;

// FCOMI(P) FUCOMI(P).
// m.
def : InstRW<[HWWrite3P01], (instrs COM_FIPr, COM_FIr, UCOM_FIPr, UCOM_FIr)>;

// FTST.
def : InstRW<[HWWriteP1], (instregex "TST_F")>;

// FXAM.
def : InstRW<[HWWrite2P1], (instrs FXAM)>;

// FPREM.
def HWWriteFPREM : SchedWriteRes<[]> {
  let Latency = 19;
  let NumMicroOps = 28;
}
def : InstRW<[HWWriteFPREM], (instrs FPREM)>;

// FPREM1.
def HWWriteFPREM1 : SchedWriteRes<[]> {
  let Latency = 27;
  let NumMicroOps = 41;
}
def : InstRW<[HWWriteFPREM1], (instrs FPREM1)>;

// FRNDINT.
def HWWriteFRNDINT : SchedWriteRes<[]> {
  let Latency = 11;
  let NumMicroOps = 17;
}
def : InstRW<[HWWriteFRNDINT], (instrs FRNDINT)>;

//-- Math instructions --//

// FSCALE.
def HWWriteFSCALE : SchedWriteRes<[]> {
  let Latency = 75; // 49-125
  let NumMicroOps = 50; // 25-75
}
def : InstRW<[HWWriteFSCALE], (instrs FSCALE)>;

// FXTRACT.
def HWWriteFXTRACT : SchedWriteRes<[]> {
  let Latency = 15;
  let NumMicroOps = 17;
}
def : InstRW<[HWWriteFXTRACT], (instrs FXTRACT)>;

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : HWWriteResPair<WriteFHAdd,  [HWPort1, HWPort5], 5, [1,2], 3, 6>;
defm : HWWriteResPair<WriteFHAddY, [HWPort1, HWPort5], 5, [1,2], 3, 7>;
defm : HWWriteResPair<WritePHAdd,  [HWPort5, HWPort15], 3, [2,1], 3, 5>;
defm : HWWriteResPair<WritePHAddX, [HWPort5, HWPort15], 3, [2,1], 3, 6>;
defm : HWWriteResPair<WritePHAddY, [HWPort5, HWPort15], 3, [2,1], 3, 7>;

//=== Floating Point XMM and YMM Instructions ===//

// Remaining instrs.

def HWWriteResGroup0 : SchedWriteRes<[HWPort23]> {
  let Latency = 6;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup0], (instrs VBROADCASTSSrm)>;
def: InstRW<[HWWriteResGroup0], (instregex "(V?)MOVSHDUPrm",
                                           "(V?)MOVSLDUPrm",
                                           "VPBROADCAST(D|Q)rm")>;

def HWWriteResGroup0_1 : SchedWriteRes<[HWPort23]> {
  let Latency = 7;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup0_1], (instrs VBROADCASTF128,
                                          VBROADCASTI128,
                                          VBROADCASTSDYrm,
                                          VBROADCASTSSYrm,
                                          VMOVDDUPYrm,
                                          VMOVSHDUPYrm,
                                          VMOVSLDUPYrm)>;
def: InstRW<[HWWriteResGroup0_1], (instregex "LD_F(32|64|80)m",
                                             "VPBROADCAST(D|Q)Yrm")>;

def HWWriteResGroup0_2 : SchedWriteRes<[HWPort23]> {
  let Latency = 5;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup0_2], (instregex "MOVSX(16|32|64)rm(8|16|32)",
                                             "MOVZX(16|32|64)rm(8|16)",
                                             "(V?)MOVDDUPrm")>;

def HWWriteResGroup1 : SchedWriteRes<[HWPort4,HWPort237]> {
  let Latency = 1;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup1], (instrs FBSTPm, VMPTRSTm)>;
def: InstRW<[HWWriteResGroup1], (instregex "ST_FP(32|64|80)m")>;

def HWWriteResGroup2 : SchedWriteRes<[HWPort0]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup2], (instregex "VPSLLVQ(Y?)rr",
                                           "VPSRLVQ(Y?)rr")>;

def HWWriteResGroup3 : SchedWriteRes<[HWPort1]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup3], (instregex "COM(P?)_FST0r",
                                           "UCOM_F(P?)r")>;

def HWWriteResGroup4 : SchedWriteRes<[HWPort5]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup4], (instrs MMX_MOVQ2DQrr)>;

def HWWriteResGroup5 : SchedWriteRes<[HWPort6]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup5], (instregex "JMP(16|32|64)r")>;

def HWWriteResGroup6 : SchedWriteRes<[HWPort01]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup6], (instrs FINCSTP, FNOP)>;

def HWWriteResGroup7 : SchedWriteRes<[HWPort06]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup7], (instrs CDQ, CQO)>;

def HWWriteResGroup8 : SchedWriteRes<[HWPort15]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup8], (instregex "ANDN(32|64)rr")>;

def HWWriteResGroup9 : SchedWriteRes<[HWPort015]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup9], (instregex "VPBLENDD(Y?)rri")>;

def HWWriteResGroup10 : SchedWriteRes<[HWPort0156]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup10], (instrs CBW, CWDE, CDQE,
                                         CMC, STC,
                                         SGDT64m,
                                         SIDT64m,
                                         SMSW16m,
                                         STRm,
                                         SYSCALL)>;

def HWWriteResGroup11 : SchedWriteRes<[HWPort0,HWPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup11], (instregex "(V?)CVTPS2PDrm")>;

def HWWriteResGroup11_1 : SchedWriteRes<[HWPort0,HWPort23]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup11_1], (instrs VPSLLVQrm, VPSRLVQrm)>;
def: InstRW<[HWWriteResGroup11_1], (instregex "(V?)CVTSS2SDrm")>;

def HWWriteResGroup11_2 : SchedWriteRes<[HWPort0,HWPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup11_2], (instrs VPSLLVQYrm, VPSRLVQYrm)>;

def HWWriteResGroup12 : SchedWriteRes<[HWPort1,HWPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup12], (instrs MMX_CVTPI2PSirm)>;
def: InstRW<[HWWriteResGroup12], (instregex "P(DEP|EXT)(32|64)rm")>;

def HWWriteResGroup13 : SchedWriteRes<[HWPort5,HWPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup13], (instregex "(V?)PMOV(SX|ZX)BDrm",
                                            "(V?)PMOV(SX|ZX)BQrm",
                                            "(V?)PMOV(SX|ZX)BWrm",
                                            "(V?)PMOV(SX|ZX)DQrm",
                                            "(V?)PMOV(SX|ZX)WDrm",
                                            "(V?)PMOV(SX|ZX)WQrm")>;

def HWWriteResGroup13_1 : SchedWriteRes<[HWPort5,HWPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup13_1], (instrs VPMOVSXBDYrm,
                                           VPMOVSXBQYrm,
                                           VPMOVSXWQYrm)>;

def HWWriteResGroup14 : SchedWriteRes<[HWPort6,HWPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup14], (instrs FARJMP64)>;
def: InstRW<[HWWriteResGroup14], (instregex "JMP(16|32|64)m")>;

def HWWriteResGroup16 : SchedWriteRes<[HWPort23,HWPort15]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup16], (instregex "ANDN(32|64)rm",
                                            "MOVBE(16|32|64)rm")>;

def HWWriteResGroup17 : SchedWriteRes<[HWPort23,HWPort015]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup17], (instrs VINSERTF128rm,
                                         VINSERTI128rm,
                                         VPBLENDDrmi)>;

def HWWriteResGroup17_2 : SchedWriteRes<[HWPort23,HWPort015]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup17_2], (instrs VPBLENDDYrmi)>;

def HWWriteResGroup18 : SchedWriteRes<[HWPort23,HWPort0156]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup18], (instrs POP16r, POP32r, POP64r)>;
def: InstRW<[HWWriteResGroup18], (instregex "POP(16|32|64)rmr")>;

def HWWriteResGroup19 : SchedWriteRes<[HWPort237,HWPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup19], (instrs SFENCE)>;

def HWWriteResGroup21 : SchedWriteRes<[HWPort4,HWPort6,HWPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup21], (instrs FNSTCW16m)>;

def HWWriteResGroup23 : SchedWriteRes<[HWPort4,HWPort237,HWPort15]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup23], (instregex "MOVBE(32|64)mr")>;

def HWWriteResGroup23_16 : SchedWriteRes<[HWPort06, HWPort237, HWPort4]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup23_16], (instrs MOVBE16mr)>;

def HWWriteResGroup24 : SchedWriteRes<[HWPort4,HWPort237,HWPort0156]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup24], (instrs PUSH16r, PUSH32r, PUSH64r, PUSH64i8,
                                         STOSB, STOSL, STOSQ, STOSW)>;
def: InstRW<[HWWriteResGroup24], (instregex "PUSH(16|32|64)rmr")>;

def HWWriteResGroup25 : SchedWriteRes<[HWPort4,HWPort23,HWPort237,HWPort06]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[HWWriteResGroup25], (instregex "SAR(8|16|32|64)m(1|i)",
                                            "SHL(8|16|32|64)m(1|i)",
                                            "SHR(8|16|32|64)m(1|i)")>;

def HWWriteResGroup26 : SchedWriteRes<[HWPort4,HWPort23,HWPort237,HWPort0156]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[HWWriteResGroup26], (instregex "POP(16|32|64)rmm",
                                            "PUSH(16|32|64)rmm")>;

def HWWriteResGroup28 : SchedWriteRes<[HWPort01]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[HWWriteResGroup28], (instrs FDECSTP)>;

def HWWriteResGroup30 : SchedWriteRes<[HWPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[HWWriteResGroup30], (instrs LFENCE,
                                         MFENCE,
                                         WAIT,
                                         XGETBV)>;

def HWWriteResGroup31 : SchedWriteRes<[HWPort0,HWPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup31], (instregex "(V?)CVTPS2PDrr",
                                            "(V?)CVTSS2SDrr")>;

def HWWriteResGroup32 : SchedWriteRes<[HWPort6,HWPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup32], (instregex "CLFLUSH")>;

def HWWriteResGroup33 : SchedWriteRes<[HWPort01,HWPort015]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup33], (instrs MMX_MOVDQ2Qrr)>;

def HWWriteResGroup35 : SchedWriteRes<[HWPort06,HWPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup35], (instrs CWD, JCXZ, JECXZ, JRCXZ)>;
def: InstRW<[HWWriteResGroup35], (instregex "SET(A|BE)r")>;

def HWWriteResGroup36_2 : SchedWriteRes<[HWPort5,HWPort23]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[HWWriteResGroup36_2], (instrs MMX_PACKSSDWirm,
                                           MMX_PACKSSWBirm,
                                           MMX_PACKUSWBirm)>;

def HWWriteResGroup37 : SchedWriteRes<[HWPort23,HWPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[HWWriteResGroup37], (instrs LEAVE, LEAVE64,
                                         SCASB, SCASL, SCASQ, SCASW)>;

def HWWriteResGroup39 : SchedWriteRes<[HWPort0,HWPort01,HWPort23]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup39], (instrs FLDCW16m)>;

def HWWriteResGroup41 : SchedWriteRes<[HWPort6,HWPort23,HWPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup41], (instrs LRETQ, RETL, RETQ)>;

def HWWriteResGroup44 : SchedWriteRes<[HWPort4,HWPort6,HWPort237,HWPort0156]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[HWWriteResGroup44], (instregex "CALL(16|32|64)r")>;

def HWWriteResGroup45 : SchedWriteRes<[HWPort4,HWPort237,HWPort06,HWPort0156]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[HWWriteResGroup45], (instrs CALL64pcrel32)>;
def: InstRW<[HWWriteResGroup45], (instregex "SET(A|BE)m")>;

def HWWriteResGroup46 : SchedWriteRes<[HWPort4,HWPort23,HWPort237,HWPort06]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,2];
}
def: InstRW<[HWWriteResGroup46], (instregex "ROL(8|16|32|64)m(1|i)",
                                            "ROR(8|16|32|64)m(1|i)")>;

def HWWriteResGroup47 : SchedWriteRes<[HWPort4,HWPort23,HWPort237,HWPort0156]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,2];
}
def: InstRW<[HWWriteResGroup47], (instregex "XADD(8|16|32|64)rm")>;

def HWWriteResGroup48 : SchedWriteRes<[HWPort4,HWPort6,HWPort23,HWPort237,HWPort0156]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,1,1];
}
def: InstRW<[HWWriteResGroup48], (instregex "CALL(16|32|64)m")>;
def: InstRW<[HWWriteResGroup48], (instrs FARCALL64)>;

def HWWriteResGroup50 : SchedWriteRes<[HWPort1]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup50], (instrs MMX_CVTPI2PSirr)>;
def: InstRW<[HWWriteResGroup50], (instregex "P(DEP|EXT)(32|64)rr",
                                            "(V?)CVTDQ2PS(Y?)rr")>;

def HWWriteResGroup51 : SchedWriteRes<[HWPort5]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup51], (instregex "VPBROADCAST(B|W)rr")>;

def HWWriteResGroup52 : SchedWriteRes<[HWPort1,HWPort23]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup52], (instregex "(V?)CVTPS2DQrm",
                                            "(V?)CVTTPS2DQrm")>;

def HWWriteResGroup52_1 : SchedWriteRes<[HWPort1,HWPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup52_1], (instregex "(ADD|SUB|SUBR)_F(32|64)m",
                                              "ILD_F(16|32|64)m")>;
def: InstRW<[HWWriteResGroup52_1], (instrs VCVTDQ2PSYrm,
                                           VCVTPS2DQYrm,
                                           VCVTTPS2DQYrm)>;

def HWWriteResGroup53_1 : SchedWriteRes<[HWPort5,HWPort23]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup53_1], (instrs VPMOVSXBWYrm,
                                           VPMOVSXDQYrm,
                                           VPMOVSXWDYrm,
                                           VPMOVZXWDYrm)>;

def HWWriteResGroup57 : SchedWriteRes<[HWPort5,HWPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[HWWriteResGroup57], (instrs MMX_PACKSSDWirr,
                                         MMX_PACKSSWBirr,
                                         MMX_PACKUSWBirr)>;

def HWWriteResGroup58 : SchedWriteRes<[HWPort6,HWPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[HWWriteResGroup58], (instregex "CLD")>;

def HWWriteResGroup59 : SchedWriteRes<[HWPort06,HWPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[HWWriteResGroup59], (instregex "RCL(8|16|32|64)r(1|i)",
                                            "RCR(8|16|32|64)r(1|i)")>;

def HWWriteResGroup61 : SchedWriteRes<[HWPort0,HWPort4,HWPort237]> {
  let Latency = 4;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup61], (instrs FNSTSWm)>;

def HWWriteResGroup62 : SchedWriteRes<[HWPort1,HWPort4,HWPort237]> {
  let Latency = 4;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup62], (instregex "IST(T?)_FP(16|32|64)m",
                                            "IST_F(16|32)m")>;

def HWWriteResGroup66 : SchedWriteRes<[HWPort23,HWPort237,HWPort06,HWPort0156]> {
  let Latency = 9;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,2];
}
def: InstRW<[HWWriteResGroup66], (instregex "RCL(8|16|32|64)m(1|i)",
                                            "RCR(8|16|32|64)m(1|i)")>;

def HWWriteResGroup68 : SchedWriteRes<[HWPort4,HWPort23,HWPort237,HWPort0156]> {
  let Latency = 9;
  let NumMicroOps = 6;
  let ResourceCycles = [1,1,1,3];
}
def: InstRW<[HWWriteResGroup68], (instregex "XCHG(8|16|32|64)rm")>;

def HWWriteResGroup69 : SchedWriteRes<[HWPort4,HWPort23,HWPort237,HWPort06,HWPort0156]> {
  let Latency = 9;
  let NumMicroOps = 6;
  let ResourceCycles = [1,1,1,2,1];
}
def: InstRW<[HWWriteResGroup69], (instregex "ROL(8|16|32|64)mCL",
                                            "ROR(8|16|32|64)mCL",
                                            "SAR(8|16|32|64)mCL",
                                            "SHL(8|16|32|64)mCL",
                                            "SHR(8|16|32|64)mCL")>;
def: SchedAlias<WriteADCRMW, HWWriteResGroup69>;

def HWWriteResGroup70 : SchedWriteRes<[HWPort0,HWPort1]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup70], (instregex "(V?)CVT(T?)SD2SI(64)?rr",
                                            "(V?)CVT(T?)SS2SI(64)?rr")>;

def HWWriteResGroup71 : SchedWriteRes<[HWPort0,HWPort5]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup71], (instrs VCVTPS2PDYrr)>;

def HWWriteResGroup72 : SchedWriteRes<[HWPort0,HWPort0156]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup72], (instrs FNSTSW16r)>;

def HWWriteResGroup73 : SchedWriteRes<[HWPort1,HWPort5]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup73], (instrs MMX_CVTPI2PDirr,
                                         MMX_CVTPD2PIirr,
                                         MMX_CVTPS2PIirr,
                                         MMX_CVTTPD2PIirr,
                                         MMX_CVTTPS2PIirr)>;
def: InstRW<[HWWriteResGroup73], (instregex "(V?)CVTDQ2PDrr",
                                            "(V?)CVTPD2PSrr",
                                            "(V?)CVTSD2SSrr",
                                            "(V?)CVTSI(64)?2SDrr",
                                            "(V?)CVTSI2SSrr",
                                            "(V?)CVT(T?)PD2DQrr")>;

def HWWriteResGroup75 : SchedWriteRes<[HWPort1,HWPort23]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[HWWriteResGroup75], (instregex "FICOM(P?)(16|32)m")>;

def HWWriteResGroup76 : SchedWriteRes<[HWPort0,HWPort1,HWPort23]> {
  let Latency = 9;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup76], (instregex "(V?)CVTSD2SI(64)?rm",
                                            "(V?)CVTSS2SI(64)?rm",
                                            "(V?)CVTTSD2SI(64)?rm",
                                            "VCVTTSS2SI64rm",
                                            "(V?)CVTTSS2SIrm")>;

def HWWriteResGroup77 : SchedWriteRes<[HWPort0,HWPort5,HWPort23]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup77], (instrs VCVTPS2PDYrm)>;

def HWWriteResGroup78 : SchedWriteRes<[HWPort1,HWPort5,HWPort23]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup78], (instrs CVTPD2PSrm,
                                         CVTPD2DQrm,
                                         CVTTPD2DQrm,
                                         MMX_CVTPD2PIirm,
                                         MMX_CVTTPD2PIirm,
                                         CVTDQ2PDrm,
                                         VCVTDQ2PDrm)>;

def HWWriteResGroup78_1 : SchedWriteRes<[HWPort1,HWPort5,HWPort23]> {
  let Latency = 9;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup78_1], (instrs MMX_CVTPI2PDirm,
                                           CVTSD2SSrm,
                                           VCVTSD2SSrm)>;

def HWWriteResGroup80 : SchedWriteRes<[HWPort5,HWPort23,HWPort015]> {
  let Latency = 9;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup80], (instregex "VPBROADCAST(B|W)(Y?)rm")>;

def HWWriteResGroup81 : SchedWriteRes<[HWPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ResourceCycles = [4];
}
def: InstRW<[HWWriteResGroup81], (instrs FNCLEX)>;

def HWWriteResGroup82 : SchedWriteRes<[]> {
  let Latency = 0;
  let NumMicroOps = 4;
  let ResourceCycles = [];
}
def: InstRW<[HWWriteResGroup82], (instrs VZEROUPPER)>;

def HWWriteResGroup83 : SchedWriteRes<[HWPort1,HWPort6,HWPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,2];
}
def: InstRW<[HWWriteResGroup83], (instregex "LAR(16|32|64)rr")>;

def HWWriteResGroup87 : SchedWriteRes<[HWPort1,HWPort6,HWPort23,HWPort0156]> {
  let Latency = 9;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[HWWriteResGroup87], (instregex "LAR(16|32|64)rm",
                                            "LSL(16|32|64)rm")>;

def HWWriteResGroup88 : SchedWriteRes<[HWPort4,HWPort237,HWPort0156]> {
  let Latency = 5;
  let NumMicroOps = 6;
  let ResourceCycles = [1,1,4];
}
def: InstRW<[HWWriteResGroup88], (instregex "PUSHF(16|64)")>;

def HWWriteResGroup89 : SchedWriteRes<[HWPort0]> {
  let Latency = 5;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup89], (instregex "(V?)PCMPGTQ(Y?)rr",
                                            "MUL_(FPrST0|FST0r|FrST0)")>;

def HWWriteResGroup91_2 : SchedWriteRes<[HWPort0,HWPort23]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup91_2], (instregex "(V?)PCMPGTQrm")>;

def HWWriteResGroup91_3 : SchedWriteRes<[HWPort0,HWPort23]> {
  let Latency = 12;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup91_3], (instregex "MUL_F(32|64)m")>;
def: InstRW<[HWWriteResGroup91_3], (instrs VPCMPGTQYrm)>;

def HWWriteResGroup93 : SchedWriteRes<[HWPort1,HWPort5]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[HWWriteResGroup93], (instregex "(V?)CVTSI642SSrr")>;

def HWWriteResGroup94 : SchedWriteRes<[HWPort1,HWPort6,HWPort06]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup94], (instregex "STR(16|32|64)r")>;

def HWWriteResGroup97 : SchedWriteRes<[HWPort0,HWPort1,HWPort5,HWPort23]> {
  let Latency = 10;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[HWWriteResGroup97], (instregex "CVTTSS2SI64rm")>;

def HWWriteResGroup99 : SchedWriteRes<[HWPort6,HWPort0156]> {
  let Latency = 5;
  let NumMicroOps = 5;
  let ResourceCycles = [1,4];
}
def: InstRW<[HWWriteResGroup99], (instrs PAUSE)>;

def HWWriteResGroup100 : SchedWriteRes<[HWPort06,HWPort0156]> {
  let Latency = 5;
  let NumMicroOps = 5;
  let ResourceCycles = [1,4];
}
def: InstRW<[HWWriteResGroup100], (instrs XSETBV)>;

def HWWriteResGroup102 : SchedWriteRes<[HWPort1,HWPort5]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup102], (instrs VCVTDQ2PDYrr,
                                          VCVTPD2PSYrr,
                                          VCVTPD2DQYrr,
                                          VCVTTPD2DQYrr)>;

def HWWriteResGroup103 : SchedWriteRes<[HWPort1,HWPort23]> {
  let Latency = 13;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[HWWriteResGroup103], (instregex "(ADD|SUB|SUBR)_FI(16|32)m")>;

def HWWriteResGroup104 : SchedWriteRes<[HWPort1,HWPort5,HWPort23]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup104], (instrs VCVTDQ2PDYrm)>;

def HWWriteResGroup107 : SchedWriteRes<[HWPort1,HWPort6,HWPort06,HWPort0156]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[HWWriteResGroup107], (instregex "SLDT(16|32|64)r")>;

def HWWriteResGroup108 : SchedWriteRes<[HWPort6,HWPort0156]> {
  let Latency = 6;
  let NumMicroOps = 6;
  let ResourceCycles = [1,5];
}
def: InstRW<[HWWriteResGroup108], (instrs STD)>;

def HWWriteResGroup114 : SchedWriteRes<[HWPort6,HWPort06,HWPort15,HWPort0156]> {
  let Latency = 7;
  let NumMicroOps = 7;
  let ResourceCycles = [2,2,1,2];
}
def: InstRW<[HWWriteResGroup114], (instrs LOOP)>;

def HWWriteResGroup115 : SchedWriteRes<[HWPort0,HWPort1,HWPort23]> {
  let Latency = 15;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup115], (instregex "MUL_FI(16|32)m")>;

def HWWriteResGroup120 : SchedWriteRes<[HWPort1,HWPort23,HWPort237,HWPort06,HWPort15,HWPort0156]> {
  let Latency = 16;
  let NumMicroOps = 10;
  let ResourceCycles = [1,1,1,4,1,2];
}
def: InstRW<[HWWriteResGroup120], (instregex "RCL(8|16|32|64)mCL")>;

def HWWriteResGroup129 : SchedWriteRes<[HWPort1,HWPort06,HWPort0156]> {
  let Latency = 11;
  let NumMicroOps = 7;
  let ResourceCycles = [2,2,3];
}
def: InstRW<[HWWriteResGroup129], (instregex "RCL(16|32|64)rCL",
                                             "RCR(16|32|64)rCL")>;

def HWWriteResGroup130 : SchedWriteRes<[HWPort1,HWPort06,HWPort15,HWPort0156]> {
  let Latency = 11;
  let NumMicroOps = 9;
  let ResourceCycles = [1,4,1,3];
}
def: InstRW<[HWWriteResGroup130], (instrs RCL8rCL)>;

def HWWriteResGroup131 : SchedWriteRes<[HWPort06,HWPort0156]> {
  let Latency = 11;
  let NumMicroOps = 11;
  let ResourceCycles = [2,9];
}
def: InstRW<[HWWriteResGroup131], (instrs LOOPE, LOOPNE)>;

def HWWriteResGroup132 : SchedWriteRes<[HWPort4,HWPort23,HWPort237,HWPort06,HWPort15,HWPort0156]> {
  let Latency = 17;
  let NumMicroOps = 14;
  let ResourceCycles = [1,1,1,4,2,5];
}
def: InstRW<[HWWriteResGroup132], (instrs CMPXCHG8B)>;

def HWWriteResGroup135 : SchedWriteRes<[HWPort1,HWPort23,HWPort237,HWPort06,HWPort15,HWPort0156]> {
  let Latency = 19;
  let NumMicroOps = 11;
  let ResourceCycles = [2,1,1,3,1,3];
}
def: InstRW<[HWWriteResGroup135], (instregex "RCR(8|16|32|64)mCL")>;

def HWWriteResGroup142 : SchedWriteRes<[HWPort1,HWPort06,HWPort15,HWPort0156]> {
  let Latency = 14;
  let NumMicroOps = 10;
  let ResourceCycles = [2,3,1,4];
}
def: InstRW<[HWWriteResGroup142], (instrs RCR8rCL)>;

def HWWriteResGroup143 : SchedWriteRes<[HWPort23,HWPort0156]> {
  let Latency = 19;
  let NumMicroOps = 15;
  let ResourceCycles = [1,14];
}
def: InstRW<[HWWriteResGroup143], (instrs POPF16)>;

def HWWriteResGroup144 : SchedWriteRes<[HWPort4,HWPort5,HWPort6,HWPort23,HWPort237,HWPort06,HWPort0156]> {
  let Latency = 21;
  let NumMicroOps = 8;
  let ResourceCycles = [1,1,1,1,1,1,2];
}
def: InstRW<[HWWriteResGroup144], (instrs INSB, INSL, INSW)>;

def HWWriteResGroup145 : SchedWriteRes<[HWPort5, HWPort6]> {
  let Latency = 8;
  let NumMicroOps = 20;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup145], (instrs VZEROALL)>;

def HWWriteResGroup146 : SchedWriteRes<[HWPort0,HWPort4,HWPort5,HWPort23,HWPort237,HWPort06,HWPort0156]> {
  let Latency = 22;
  let NumMicroOps = 19;
  let ResourceCycles = [2,1,4,1,1,4,6];
}
def: InstRW<[HWWriteResGroup146], (instrs CMPXCHG16B)>;

def HWWriteResGroup147 : SchedWriteRes<[HWPort0,HWPort1,HWPort5,HWPort6,HWPort01,HWPort0156]> {
  let Latency = 17;
  let NumMicroOps = 15;
  let ResourceCycles = [2,1,2,4,2,4];
}
def: InstRW<[HWWriteResGroup147], (instrs XCH_F)>;

def HWWriteResGroup149 : SchedWriteRes<[HWPort5,HWPort6,HWPort06,HWPort0156]> {
  let Latency = 18;
  let NumMicroOps = 8;
  let ResourceCycles = [1,1,1,5];
}
def: InstRW<[HWWriteResGroup149], (instrs CPUID, RDTSC)>;

def HWWriteResGroup151 : SchedWriteRes<[HWPort6,HWPort23,HWPort0156]> {
  let Latency = 23;
  let NumMicroOps = 19;
  let ResourceCycles = [3,1,15];
}
def: InstRW<[HWWriteResGroup151], (instregex "XRSTOR(64)?")>;

def HWWriteResGroup154 : SchedWriteRes<[HWPort0]> {
  let Latency = 20;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup154], (instregex "DIV_(FPrST0|FST0r|FrST0)")>;

def HWWriteResGroup155 : SchedWriteRes<[HWPort0,HWPort23]> {
  let Latency = 27;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup155], (instregex "DIVR_F(32|64)m")>;

def HWWriteResGroup156 : SchedWriteRes<[HWPort5,HWPort6,HWPort0156]> {
  let Latency = 20;
  let NumMicroOps = 10;
  let ResourceCycles = [1,2,7];
}
def: InstRW<[HWWriteResGroup156], (instrs MWAITrr)>;

def HWWriteResGroup161 : SchedWriteRes<[HWPort0,HWPort1,HWPort23]> {
  let Latency = 30;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup161], (instregex "DIVR_FI(16|32)m")>;

def HWWriteResGroup162 : SchedWriteRes<[HWPort0]> {
  let Latency = 24;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[HWWriteResGroup162], (instregex "DIVR_(FPrST0|FST0r|FrST0)")>;

def HWWriteResGroup163 : SchedWriteRes<[HWPort0,HWPort23]> {
  let Latency = 31;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[HWWriteResGroup163], (instregex "DIV_F(32|64)m")>;

def HWWriteResGroup164 : SchedWriteRes<[HWPort4,HWPort6,HWPort23,HWPort237,HWPort0156]> {
  let Latency = 30;
  let NumMicroOps = 27;
  let ResourceCycles = [1,5,1,1,19];
}
def: InstRW<[HWWriteResGroup164], (instrs XSAVE64)>;

def HWWriteResGroup165 : SchedWriteRes<[HWPort4,HWPort6,HWPort23,HWPort237,HWPort0156]> {
  let Latency = 31;
  let NumMicroOps = 28;
  let ResourceCycles = [1,6,1,1,19];
}
def: InstRW<[HWWriteResGroup165], (instrs XSAVE)>;
def: InstRW<[HWWriteResGroup165], (instregex "XSAVEC", "XSAVES", "XSAVEOPT")>;

def HWWriteResGroup166 : SchedWriteRes<[HWPort0,HWPort1,HWPort23]> {
  let Latency = 34;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[HWWriteResGroup166], (instregex "DIV_FI(16|32)m")>;

def HWWriteResGroup170 : SchedWriteRes<[HWPort5,HWPort6,HWPort23,HWPort06,HWPort0156]> {
  let Latency = 35;
  let NumMicroOps = 23;
  let ResourceCycles = [1,5,3,4,10];
}
def: InstRW<[HWWriteResGroup170], (instregex "IN(8|16|32)ri",
                                             "IN(8|16|32)rr")>;

def HWWriteResGroup171 : SchedWriteRes<[HWPort5,HWPort6,HWPort23,HWPort237,HWPort06,HWPort0156]> {
  let Latency = 36;
  let NumMicroOps = 23;
  let ResourceCycles = [1,5,2,1,4,10];
}
def: InstRW<[HWWriteResGroup171], (instregex "OUT(8|16|32)ir",
                                             "OUT(8|16|32)rr")>;

def HWWriteResGroup175 : SchedWriteRes<[HWPort1,HWPort4,HWPort5,HWPort6,HWPort23,HWPort237,HWPort15,HWPort0156]> {
  let Latency = 41;
  let NumMicroOps = 18;
  let ResourceCycles = [1,1,2,3,1,1,1,8];
}
def: InstRW<[HWWriteResGroup175], (instrs VMCLEARm)>;

def HWWriteResGroup176 : SchedWriteRes<[HWPort5,HWPort0156]> {
  let Latency = 42;
  let NumMicroOps = 22;
  let ResourceCycles = [2,20];
}
def: InstRW<[HWWriteResGroup176], (instrs RDTSCP)>;

def HWWriteResGroup177 : SchedWriteRes<[HWPort0,HWPort01,HWPort23,HWPort05,HWPort06,HWPort015,HWPort0156]> {
  let Latency = 61;
  let NumMicroOps = 64;
  let ResourceCycles = [2,2,8,1,10,2,39];
}
def: InstRW<[HWWriteResGroup177], (instrs FLDENVm)>;

def HWWriteResGroup178 : SchedWriteRes<[HWPort0,HWPort6,HWPort23,HWPort05,HWPort06,HWPort15,HWPort0156]> {
  let Latency = 64;
  let NumMicroOps = 88;
  let ResourceCycles = [4,4,31,1,2,1,45];
}
def: InstRW<[HWWriteResGroup178], (instrs FXRSTOR64)>;

def HWWriteResGroup179 : SchedWriteRes<[HWPort0,HWPort6,HWPort23,HWPort05,HWPort06,HWPort15,HWPort0156]> {
  let Latency = 64;
  let NumMicroOps = 90;
  let ResourceCycles = [4,2,33,1,2,1,47];
}
def: InstRW<[HWWriteResGroup179], (instrs FXRSTOR)>;

def HWWriteResGroup180 : SchedWriteRes<[HWPort5,HWPort01,HWPort0156]> {
  let Latency = 75;
  let NumMicroOps = 15;
  let ResourceCycles = [6,3,6];
}
def: InstRW<[HWWriteResGroup180], (instrs FNINIT)>;

def HWWriteResGroup183 : SchedWriteRes<[HWPort0,HWPort1,HWPort4,HWPort5,HWPort6,HWPort237,HWPort06,HWPort0156]> {
  let Latency = 115;
  let NumMicroOps = 100;
  let ResourceCycles = [9,9,11,8,1,11,21,30];
}
def: InstRW<[HWWriteResGroup183], (instrs FSTENVm)>;

def HWWriteResGroup184 : SchedWriteRes<[HWPort0, HWPort5, HWPort15, HWPort015, HWPort06, HWPort23]> {
  let Latency = 26;
  let NumMicroOps = 12;
  let ResourceCycles = [2,2,1,3,2,2];
}
def: InstRW<[HWWriteResGroup184], (instrs VGATHERDPDrm,
                                          VPGATHERDQrm,
                                          VPGATHERDDrm)>;

def HWWriteResGroup185 : SchedWriteRes<[HWPort0, HWPort5, HWPort06, HWPort15, HWPort015, HWPort23]> {
  let Latency = 24;
  let NumMicroOps = 22;
  let ResourceCycles = [5,3,4,1,5,4];
}
def: InstRW<[HWWriteResGroup185], (instrs VGATHERQPDYrm,
                                          VPGATHERQQYrm)>;

def HWWriteResGroup186 : SchedWriteRes<[HWPort0, HWPort5, HWPort06, HWPort15, HWPort015, HWPort23]> {
  let Latency = 28;
  let NumMicroOps = 22;
  let ResourceCycles = [5,3,4,1,5,4];
}
def: InstRW<[HWWriteResGroup186], (instrs VPGATHERQDYrm)>;

def HWWriteResGroup187 : SchedWriteRes<[HWPort0, HWPort5, HWPort06, HWPort15, HWPort015, HWPort23]> {
  let Latency = 25;
  let NumMicroOps = 22;
  let ResourceCycles = [5,3,4,1,5,4];
}
def: InstRW<[HWWriteResGroup187], (instrs VPGATHERQDrm)>;

def HWWriteResGroup188 : SchedWriteRes<[HWPort0, HWPort5, HWPort06, HWPort15, HWPort015, HWPort23]> {
  let Latency = 27;
  let NumMicroOps = 20;
  let ResourceCycles = [3,3,4,1,5,4];
}
def: InstRW<[HWWriteResGroup188], (instrs VGATHERDPDYrm,
                                          VPGATHERDQYrm)>;

def HWWriteResGroup189 : SchedWriteRes<[HWPort0, HWPort5, HWPort06, HWPort15, HWPort015, HWPort23]> {
  let Latency = 27;
  let NumMicroOps = 34;
  let ResourceCycles = [5,3,8,1,9,8];
}
def: InstRW<[HWWriteResGroup189], (instrs VGATHERDPSYrm,
                                          VPGATHERDDYrm)>;

def HWWriteResGroup190 : SchedWriteRes<[HWPort0, HWPort5, HWPort06, HWPort15, HWPort015, HWPort23]> {
  let Latency = 23;
  let NumMicroOps = 14;
  let ResourceCycles = [3,3,2,1,3,2];
}
def: InstRW<[HWWriteResGroup190], (instrs VGATHERQPDrm,
                                          VPGATHERQQrm)>;

def HWWriteResGroup191 : SchedWriteRes<[HWPort0, HWPort5, HWPort06, HWPort15, HWPort015, HWPort23]> {
  let Latency = 28;
  let NumMicroOps = 15;
  let ResourceCycles = [3,3,2,1,4,2];
}
def: InstRW<[HWWriteResGroup191], (instrs VGATHERQPSYrm)>;

def HWWriteResGroup192 : SchedWriteRes<[HWPort0, HWPort5, HWPort06, HWPort15, HWPort015, HWPort23]> {
  let Latency = 25;
  let NumMicroOps = 15;
  let ResourceCycles = [3,3,2,1,4,2];
}
def: InstRW<[HWWriteResGroup192], (instrs VGATHERQPSrm,
                                          VGATHERDPSrm)>;

def: InstRW<[WriteZero], (instrs CLC)>;

} // SchedModel
