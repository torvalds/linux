//=- X86SchedSkylake.td - X86 Skylake Server Scheduling ------*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for Skylake Server to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

def SkylakeServerModel : SchedMachineModel {
  // All x86 instructions are modeled as a single micro-op, and SKylake can
  // decode 6 instructions per cycle.
  let IssueWidth = 6;
  let MicroOpBufferSize = 224; // Based on the reorder buffer.
  let LoadLatency = 5;
  let MispredictPenalty = 14;

  // Based on the LSD (loop-stream detector) queue size and benchmarking data.
  let LoopMicroOpBufferSize = 50;

  // This flag is set to allow the scheduler to assign a default model to
  // unrecognized opcodes.
  let CompleteModel = 0;
}

let SchedModel = SkylakeServerModel in {

// Skylake Server can issue micro-ops to 8 different ports in one cycle.

// Ports 0, 1, 5, and 6 handle all computation.
// Port 4 gets the data half of stores. Store data can be available later than
// the store address, but since we don't model the latency of stores, we can
// ignore that.
// Ports 2 and 3 are identical. They handle loads and the address half of
// stores. Port 7 can handle address calculations.
def SKXPort0 : ProcResource<1>;
def SKXPort1 : ProcResource<1>;
def SKXPort2 : ProcResource<1>;
def SKXPort3 : ProcResource<1>;
def SKXPort4 : ProcResource<1>;
def SKXPort5 : ProcResource<1>;
def SKXPort6 : ProcResource<1>;
def SKXPort7 : ProcResource<1>;

// Many micro-ops are capable of issuing on multiple ports.
def SKXPort01  : ProcResGroup<[SKXPort0, SKXPort1]>;
def SKXPort23  : ProcResGroup<[SKXPort2, SKXPort3]>;
def SKXPort237 : ProcResGroup<[SKXPort2, SKXPort3, SKXPort7]>;
def SKXPort04  : ProcResGroup<[SKXPort0, SKXPort4]>;
def SKXPort05  : ProcResGroup<[SKXPort0, SKXPort5]>;
def SKXPort06  : ProcResGroup<[SKXPort0, SKXPort6]>;
def SKXPort15  : ProcResGroup<[SKXPort1, SKXPort5]>;
def SKXPort16  : ProcResGroup<[SKXPort1, SKXPort6]>;
def SKXPort56  : ProcResGroup<[SKXPort5, SKXPort6]>;
def SKXPort015 : ProcResGroup<[SKXPort0, SKXPort1, SKXPort5]>;
def SKXPort056 : ProcResGroup<[SKXPort0, SKXPort5, SKXPort6]>;
def SKXPort0156: ProcResGroup<[SKXPort0, SKXPort1, SKXPort5, SKXPort6]>;

def SKXDivider : ProcResource<1>; // Integer division issued on port 0.
// FP division and sqrt on port 0.
def SKXFPDivider : ProcResource<1>;

// 60 Entry Unified Scheduler
def SKXPortAny : ProcResGroup<[SKXPort0, SKXPort1, SKXPort2, SKXPort3, SKXPort4,
                              SKXPort5, SKXPort6, SKXPort7]> {
  let BufferSize=60;
}

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
multiclass SKXWriteResPair<X86FoldableSchedWrite SchedRW,
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
  def : WriteRes<SchedRW.Folded, !listconcat([SKXPort23], ExePorts)> {
    let Latency = !add(Lat, LoadLat);
    let ResourceCycles = !listconcat([1], Res);
    let NumMicroOps = !add(UOps, 1);
  }
}

// A folded store needs a cycle on port 4 for the store data, and an extra port
// 2/3/7 cycle to recompute the address.
def : WriteRes<WriteRMW, [SKXPort237,SKXPort4]>;

// Arithmetic.
defm : SKXWriteResPair<WriteALU,    [SKXPort0156], 1>; // Simple integer ALU op.
defm : SKXWriteResPair<WriteADC,    [SKXPort06],   1>; // Integer ALU + flags op.

// Integer multiplication.
defm : SKXWriteResPair<WriteIMul8,     [SKXPort1],   3>;
defm : SKXWriteResPair<WriteIMul16,    [SKXPort1,SKXPort06,SKXPort0156], 4, [1,1,2], 4>;
defm : X86WriteRes<WriteIMul16Imm,     [SKXPort1,SKXPort0156], 4, [1,1], 2>;
defm : X86WriteRes<WriteIMul16ImmLd,   [SKXPort1,SKXPort0156,SKXPort23], 8, [1,1,1], 3>;
defm : X86WriteRes<WriteIMul16Reg,     [SKXPort1],   3, [1], 1>;
defm : X86WriteRes<WriteIMul16RegLd,   [SKXPort1,SKXPort0156,SKXPort23], 8, [1,1,1], 3>;
defm : SKXWriteResPair<WriteIMul32,    [SKXPort1,SKXPort06,SKXPort0156], 4, [1,1,1], 3>;
defm : SKXWriteResPair<WriteIMul32Imm, [SKXPort1],   3>;
defm : SKXWriteResPair<WriteIMul32Reg, [SKXPort1],   3>;
defm : SKXWriteResPair<WriteIMul64,    [SKXPort1,SKXPort5], 4, [1,1], 2>;
defm : SKXWriteResPair<WriteIMul64Imm, [SKXPort1],   3>;
defm : SKXWriteResPair<WriteIMul64Reg, [SKXPort1],   3>;
def : WriteRes<WriteIMulH, []> { let Latency = 3; }

defm : X86WriteRes<WriteBSWAP32, [SKXPort15], 1, [1], 1>;
defm : X86WriteRes<WriteBSWAP64, [SKXPort06, SKXPort15], 2, [1,1], 2>;
defm : X86WriteRes<WriteCMPXCHG,[SKXPort06, SKXPort0156], 5, [2,3], 5>;
defm : X86WriteRes<WriteCMPXCHGRMW,[SKXPort23,SKXPort06,SKXPort0156,SKXPort237,SKXPort4], 8, [1,2,1,1,1], 6>;
defm : X86WriteRes<WriteXCHG,       [SKXPort0156], 2, [3], 3>;

// TODO: Why isn't the SKXDivider used?
defm : SKXWriteResPair<WriteDiv8,  [SKXPort0, SKXDivider], 25, [1,10], 1, 4>;
defm : X86WriteRes<WriteDiv16,     [SKXPort0,SKXPort1,SKXPort5,SKXPort6,SKXPort05,SKXPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv32,     [SKXPort0,SKXPort1,SKXPort5,SKXPort6,SKXPort05,SKXPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv64,     [SKXPort0,SKXPort1,SKXPort5,SKXPort6,SKXPort05,SKXPort0156], 76, [7,2,8,3,1,11], 32>;
defm : X86WriteRes<WriteDiv16Ld,   [SKXPort0,SKXPort23,SKXDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv32Ld,   [SKXPort0,SKXPort23,SKXDivider], 29, [1,1,10], 2>;
defm : X86WriteRes<WriteDiv64Ld,   [SKXPort0,SKXPort23,SKXDivider], 29, [1,1,10], 2>;

defm : X86WriteRes<WriteIDiv8,     [SKXPort0, SKXDivider], 25, [1,10], 1>;
defm : X86WriteRes<WriteIDiv16,    [SKXPort0,SKXPort1,SKXPort5,SKXPort6,SKXPort06,SKXPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv32,    [SKXPort0,SKXPort1,SKXPort5,SKXPort6,SKXPort06,SKXPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv64,    [SKXPort0,SKXPort1,SKXPort5,SKXPort6,SKXPort06,SKXPort0156], 102, [4,2,4,8,14,34], 66>;
defm : X86WriteRes<WriteIDiv8Ld,   [SKXPort0,SKXPort5,SKXPort23,SKXPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv16Ld,  [SKXPort0,SKXPort5,SKXPort23,SKXPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv32Ld,  [SKXPort0,SKXPort5,SKXPort23,SKXPort0156], 28, [2,4,1,1], 8>;
defm : X86WriteRes<WriteIDiv64Ld,  [SKXPort0,SKXPort5,SKXPort23,SKXPort0156], 28, [2,4,1,1], 8>;

defm : SKXWriteResPair<WriteCRC32, [SKXPort1], 3>;

def : WriteRes<WriteLEA, [SKXPort15]>; // LEA instructions can't fold loads.

defm : SKXWriteResPair<WriteCMOV,  [SKXPort06], 1, [1], 1>; // Conditional move.
defm : SKXWriteResPair<WriteCMOV2, [SKXPort06], 2, [2], 2>; // Conditional (CF + ZF flag) move.
defm : X86WriteRes<WriteFCMOV, [SKXPort1], 3, [1], 1>; // x87 conditional move.
def  : WriteRes<WriteSETCC, [SKXPort06]>; // Setcc.
def  : WriteRes<WriteSETCCStore, [SKXPort06,SKXPort4,SKXPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
}
defm : X86WriteRes<WriteLAHFSAHF,        [SKXPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTest,         [SKXPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestImmLd,    [SKXPort06,SKXPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteBitTestRegLd,    [SKXPort0156,SKXPort23], 6, [1,1], 2>;
defm : X86WriteRes<WriteBitTestSet,      [SKXPort06], 1, [1], 1>;
defm : X86WriteRes<WriteBitTestSetImmLd, [SKXPort06,SKXPort23], 5, [1,1], 3>;
defm : X86WriteRes<WriteBitTestSetRegLd, [SKXPort0156,SKXPort23], 5, [1,1], 2>;

// Integer shifts and rotates.
defm : SKXWriteResPair<WriteShift,    [SKXPort06],  1>;
defm : SKXWriteResPair<WriteShiftCL,  [SKXPort06],  3, [3], 3>;
defm : SKXWriteResPair<WriteRotate,   [SKXPort06],  2, [2], 2>;
defm : SKXWriteResPair<WriteRotateCL, [SKXPort06],  3, [3], 3>;

// SHLD/SHRD.
defm : X86WriteRes<WriteSHDrri, [SKXPort1], 3, [1], 1>;
defm : X86WriteRes<WriteSHDrrcl,[SKXPort1,SKXPort06,SKXPort0156], 6, [1, 2, 1], 4>;
defm : X86WriteRes<WriteSHDmri, [SKXPort1,SKXPort23,SKXPort237,SKXPort0156], 9, [1, 1, 1, 1], 4>;
defm : X86WriteRes<WriteSHDmrcl,[SKXPort1,SKXPort23,SKXPort237,SKXPort06,SKXPort0156], 11, [1, 1, 1, 2, 1], 6>;

// Bit counts.
defm : SKXWriteResPair<WriteBSF, [SKXPort1], 3>;
defm : SKXWriteResPair<WriteBSR, [SKXPort1], 3>;
defm : SKXWriteResPair<WriteLZCNT,          [SKXPort1], 3>;
defm : SKXWriteResPair<WriteTZCNT,          [SKXPort1], 3>;
defm : SKXWriteResPair<WritePOPCNT,         [SKXPort1], 3>;

// BMI1 BEXTR/BLS, BMI2 BZHI
defm : SKXWriteResPair<WriteBEXTR, [SKXPort06,SKXPort15], 2, [1,1], 2>;
defm : SKXWriteResPair<WriteBLS,   [SKXPort15], 1>;
defm : SKXWriteResPair<WriteBZHI,  [SKXPort15], 1>;

// Loads, stores, and moves, not folded with other operations.
defm : X86WriteRes<WriteLoad,    [SKXPort23], 5, [1], 1>;
defm : X86WriteRes<WriteStore,   [SKXPort237, SKXPort4], 1, [1,1], 1>;
defm : X86WriteRes<WriteStoreNT, [SKXPort237, SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteMove,    [SKXPort0156], 1, [1], 1>;

// Idioms that clear a register, like xorps %xmm0, %xmm0.
// These can often bypass execution ports completely.
def : WriteRes<WriteZero,  []>;

// Branches don't produce values, so they have no latency, but they still
// consume resources. Indirect branches can fold loads.
defm : SKXWriteResPair<WriteJump,  [SKXPort06],   1>;

// Floating point. This covers both scalar and vector operations.
defm : X86WriteRes<WriteFLD0,          [SKXPort05], 1, [1], 1>;
defm : X86WriteRes<WriteFLD1,          [SKXPort05], 1, [2], 2>;
defm : X86WriteRes<WriteFLDC,          [SKXPort05], 1, [2], 2>;
defm : X86WriteRes<WriteFLoad,         [SKXPort23], 5, [1], 1>;
defm : X86WriteRes<WriteFLoadX,        [SKXPort23], 6, [1], 1>;
defm : X86WriteRes<WriteFLoadY,        [SKXPort23], 7, [1], 1>;
defm : X86WriteRes<WriteFMaskedLoad,   [SKXPort23,SKXPort015], 7, [1,1], 2>;
defm : X86WriteRes<WriteFMaskedLoadY,  [SKXPort23,SKXPort015], 8, [1,1], 2>;
defm : X86WriteRes<WriteFStore,        [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreX,       [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreY,       [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNT,      [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNTX,     [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFStoreNTY,     [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteFMaskedStore,  [SKXPort237,SKXPort0], 2, [1,1], 2>;
defm : X86WriteRes<WriteFMaskedStoreY, [SKXPort237,SKXPort0], 2, [1,1], 2>;
defm : X86WriteRes<WriteFMove,         [SKXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveX,        [SKXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteFMoveY,        [SKXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteEMMS,          [SKXPort05,SKXPort0156], 10, [9,1], 10>;

defm : SKXWriteResPair<WriteFAdd,      [SKXPort01],  4, [1], 1, 5>; // Floating point add/sub.
defm : SKXWriteResPair<WriteFAddX,     [SKXPort01],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFAddY,     [SKXPort01],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFAddZ,     [SKXPort05],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFAdd64,    [SKXPort01],  4, [1], 1, 5>; // Floating point double add/sub.
defm : SKXWriteResPair<WriteFAdd64X,   [SKXPort01],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFAdd64Y,   [SKXPort01],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFAdd64Z,   [SKXPort05],  4, [1], 1, 7>;

defm : SKXWriteResPair<WriteFCmp,      [SKXPort01],  4, [1], 1, 5>; // Floating point compare.
defm : SKXWriteResPair<WriteFCmpX,     [SKXPort01],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFCmpY,     [SKXPort01],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFCmpZ,     [SKXPort05],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFCmp64,    [SKXPort01],  4, [1], 1, 5>; // Floating point double compare.
defm : SKXWriteResPair<WriteFCmp64X,   [SKXPort01],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFCmp64Y,   [SKXPort01],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFCmp64Z,   [SKXPort05],  4, [1], 1, 7>;

defm : SKXWriteResPair<WriteFCom,       [SKXPort0],  2>; // Floating point compare to flags.

defm : SKXWriteResPair<WriteFMul,      [SKXPort01],  4, [1], 1, 5>; // Floating point multiplication.
defm : SKXWriteResPair<WriteFMulX,     [SKXPort01],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFMulY,     [SKXPort01],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFMulZ,     [SKXPort05],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFMul64,    [SKXPort01],  4, [1], 1, 5>; // Floating point double multiplication.
defm : SKXWriteResPair<WriteFMul64X,   [SKXPort01],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFMul64Y,   [SKXPort01],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFMul64Z,   [SKXPort05],  4, [1], 1, 7>;

defm : SKXWriteResPair<WriteFDiv,     [SKXPort0,SKXFPDivider], 11, [1,3], 1, 5>; // 10-14 cycles. // Floating point division.
//defm : SKXWriteResPair<WriteFDivX,    [SKXPort0,SKXFPDivider], 11, [1,3], 1, 6>; // 10-14 cycles.
defm : SKXWriteResPair<WriteFDivY,    [SKXPort0,SKXFPDivider], 11, [1,5], 1, 7>; // 10-14 cycles.
defm : SKXWriteResPair<WriteFDivZ,    [SKXPort0,SKXPort5,SKXFPDivider], 18, [2,1,10], 3, 7>; // 10-14 cycles.
//defm : SKXWriteResPair<WriteFDiv64,   [SKXPort0,SKXFPDivider], 14, [1,3], 1, 5>; // 10-14 cycles. // Floating point division.
//defm : SKXWriteResPair<WriteFDiv64X,  [SKXPort0,SKXFPDivider], 14, [1,3], 1, 6>; // 10-14 cycles.
//defm : SKXWriteResPair<WriteFDiv64Y,  [SKXPort0,SKXFPDivider], 14, [1,5], 1, 7>; // 10-14 cycles.
defm : SKXWriteResPair<WriteFDiv64Z,  [SKXPort0,SKXPort5,SKXFPDivider], 23, [2,1,16], 3, 7>; // 10-14 cycles.

defm : SKXWriteResPair<WriteFSqrt,    [SKXPort0,SKXFPDivider], 12, [1,3], 1, 5>; // Floating point square root.
defm : SKXWriteResPair<WriteFSqrtX,   [SKXPort0,SKXFPDivider], 12, [1,3], 1, 6>;
defm : SKXWriteResPair<WriteFSqrtY,   [SKXPort0,SKXFPDivider], 12, [1,6], 1, 7>;
defm : SKXWriteResPair<WriteFSqrtZ,   [SKXPort0,SKXPort5,SKXFPDivider], 20, [2,1,12], 3, 7>;
defm : SKXWriteResPair<WriteFSqrt64,  [SKXPort0,SKXFPDivider], 18, [1,6], 1, 5>; // Floating point double square root.
defm : SKXWriteResPair<WriteFSqrt64X, [SKXPort0,SKXFPDivider], 18, [1,6], 1, 6>;
defm : SKXWriteResPair<WriteFSqrt64Y, [SKXPort0,SKXFPDivider], 18, [1,12],1, 7>;
defm : SKXWriteResPair<WriteFSqrt64Z, [SKXPort0,SKXPort5,SKXFPDivider], 32, [2,1,24], 3, 7>;
defm : SKXWriteResPair<WriteFSqrt80,  [SKXPort0,SKXFPDivider], 21, [1,7]>; // Floating point long double square root.

defm : SKXWriteResPair<WriteFRcp,   [SKXPort0],  4, [1], 1, 5>; // Floating point reciprocal estimate.
defm : SKXWriteResPair<WriteFRcpX,  [SKXPort0],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFRcpY,  [SKXPort0],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFRcpZ,  [SKXPort0,SKXPort5],  4, [2,1], 3, 7>;

defm : SKXWriteResPair<WriteFRsqrt, [SKXPort0],  4, [1], 1, 5>; // Floating point reciprocal square root estimate.
defm : SKXWriteResPair<WriteFRsqrtX,[SKXPort0],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFRsqrtY,[SKXPort0],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFRsqrtZ,[SKXPort0,SKXPort5],  9, [2,1], 3, 7>;

defm : SKXWriteResPair<WriteFMA,  [SKXPort01],  4, [1], 1, 5>; // Fused Multiply Add.
defm : SKXWriteResPair<WriteFMAX, [SKXPort01],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteFMAY, [SKXPort01],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteFMAZ, [SKXPort05],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteDPPD, [SKXPort5,SKXPort015],  9, [1,2], 3, 6>; // Floating point double dot product.
defm : SKXWriteResPair<WriteDPPS, [SKXPort5,SKXPort015], 13, [1,3], 4, 6>;
defm : SKXWriteResPair<WriteDPPSY,[SKXPort5,SKXPort015], 13, [1,3], 4, 7>;
defm : SKXWriteResPair<WriteDPPSZ,[SKXPort5,SKXPort015], 13, [1,3], 4, 7>;
defm : SKXWriteResPair<WriteFSign,  [SKXPort0],  1>; // Floating point fabs/fchs.
defm : SKXWriteResPair<WriteFRnd,   [SKXPort01], 8, [2], 2, 6>; // Floating point rounding.
defm : SKXWriteResPair<WriteFRndY,  [SKXPort01], 8, [2], 2, 7>;
defm : SKXWriteResPair<WriteFRndZ,  [SKXPort05], 8, [2], 2, 7>;
defm : SKXWriteResPair<WriteFLogic, [SKXPort015], 1, [1], 1, 6>; // Floating point and/or/xor logicals.
defm : SKXWriteResPair<WriteFLogicY, [SKXPort015], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteFLogicZ, [SKXPort05], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteFTest,  [SKXPort0], 2, [1], 1, 6>; // Floating point TEST instructions.
defm : SKXWriteResPair<WriteFTestY, [SKXPort0], 2, [1], 1, 7>;
defm : SKXWriteResPair<WriteFTestZ, [SKXPort0], 2, [1], 1, 7>;
defm : SKXWriteResPair<WriteFShuffle,  [SKXPort5], 1, [1], 1, 6>; // Floating point vector shuffles.
defm : SKXWriteResPair<WriteFShuffleY, [SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteFShuffleZ, [SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteFVarShuffle,  [SKXPort5], 1, [1], 1, 6>; // Floating point vector variable shuffles.
defm : SKXWriteResPair<WriteFVarShuffleY, [SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteFVarShuffleZ, [SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteFBlend, [SKXPort015], 1, [1], 1, 6>; // Floating point vector blends.
defm : SKXWriteResPair<WriteFBlendY,[SKXPort015], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteFBlendZ,[SKXPort015], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteFVarBlend, [SKXPort015], 2, [2], 2, 6>; // Fp vector variable blends.
defm : SKXWriteResPair<WriteFVarBlendY,[SKXPort015], 2, [2], 2, 7>;
defm : SKXWriteResPair<WriteFVarBlendZ,[SKXPort015], 2, [2], 2, 7>;

// FMA Scheduling helper class.
// class FMASC { X86FoldableSchedWrite Sched = WriteFAdd; }

// Vector integer operations.
defm : X86WriteRes<WriteVecLoad,         [SKXPort23], 5, [1], 1>;
defm : X86WriteRes<WriteVecLoadX,        [SKXPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadY,        [SKXPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecLoadNT,       [SKXPort23], 6, [1], 1>;
defm : X86WriteRes<WriteVecLoadNTY,      [SKXPort23], 7, [1], 1>;
defm : X86WriteRes<WriteVecMaskedLoad,   [SKXPort23,SKXPort015], 7, [1,1], 2>;
defm : X86WriteRes<WriteVecMaskedLoadY,  [SKXPort23,SKXPort015], 8, [1,1], 2>;
defm : X86WriteRes<WriteVecStore,        [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreX,       [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreY,       [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreNT,      [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecStoreNTY,     [SKXPort237,SKXPort4], 1, [1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStore,  [SKXPort237,SKXPort0], 2, [1,1], 2>;
defm : X86WriteRes<WriteVecMaskedStoreY, [SKXPort237,SKXPort0], 2, [1,1], 2>;
defm : X86WriteRes<WriteVecMove,         [SKXPort05],  1, [1], 1>;
defm : X86WriteRes<WriteVecMoveX,        [SKXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveY,        [SKXPort015], 1, [1], 1>;
defm : X86WriteRes<WriteVecMoveToGpr,    [SKXPort0], 2, [1], 1>;
defm : X86WriteRes<WriteVecMoveFromGpr,  [SKXPort5], 1, [1], 1>;

defm : SKXWriteResPair<WriteVecALU,   [SKXPort05], 1, [1], 1, 5>; // Vector integer ALU op, no logicals.
defm : SKXWriteResPair<WriteVecALUX,  [SKXPort01], 1, [1], 1, 6>;
defm : SKXWriteResPair<WriteVecALUY,  [SKXPort01], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVecALUZ,  [SKXPort0], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVecLogic, [SKXPort05],  1, [1], 1, 5>; // Vector integer and/or/xor.
defm : SKXWriteResPair<WriteVecLogicX,[SKXPort015], 1, [1], 1, 6>;
defm : SKXWriteResPair<WriteVecLogicY,[SKXPort015], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVecLogicZ,[SKXPort05], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVecTest,  [SKXPort0,SKXPort5], 3, [1,1], 2, 6>; // Vector integer TEST instructions.
defm : SKXWriteResPair<WriteVecTestY, [SKXPort0,SKXPort5], 3, [1,1], 2, 7>;
defm : SKXWriteResPair<WriteVecTestZ, [SKXPort0,SKXPort5], 3, [1,1], 2, 7>;
defm : SKXWriteResPair<WriteVecIMul,  [SKXPort0],    4, [1], 1, 5>; // Vector integer multiply.
defm : SKXWriteResPair<WriteVecIMulX, [SKXPort01],  4, [1], 1, 6>;
defm : SKXWriteResPair<WriteVecIMulY, [SKXPort01],  4, [1], 1, 7>;
defm : SKXWriteResPair<WriteVecIMulZ, [SKXPort05],  4, [1], 1, 7>;
defm : SKXWriteResPair<WritePMULLD,   [SKXPort01], 10, [2], 2, 6>; // Vector PMULLD.
defm : SKXWriteResPair<WritePMULLDY,  [SKXPort01], 10, [2], 2, 7>;
defm : SKXWriteResPair<WritePMULLDZ,  [SKXPort05], 10, [2], 2, 7>;
defm : SKXWriteResPair<WriteShuffle,  [SKXPort5], 1, [1], 1, 5>; // Vector shuffles.
defm : SKXWriteResPair<WriteShuffleX, [SKXPort5], 1, [1], 1, 6>;
defm : SKXWriteResPair<WriteShuffleY, [SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteShuffleZ, [SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVarShuffle,  [SKXPort5], 1, [1], 1, 5>; // Vector variable shuffles.
defm : SKXWriteResPair<WriteVarShuffleX, [SKXPort5], 1, [1], 1, 6>;
defm : SKXWriteResPair<WriteVarShuffleY, [SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVarShuffleZ, [SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteBlend, [SKXPort5], 1, [1], 1, 6>; // Vector blends.
defm : SKXWriteResPair<WriteBlendY,[SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteBlendZ,[SKXPort5], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVarBlend, [SKXPort015], 2, [2], 2, 6>; // Vector variable blends.
defm : SKXWriteResPair<WriteVarBlendY,[SKXPort015], 2, [2], 2, 6>;
defm : SKXWriteResPair<WriteVarBlendZ,[SKXPort05],  2, [1], 1, 6>;
defm : SKXWriteResPair<WriteMPSAD,   [SKXPort5], 4, [2], 2, 6>; // Vector MPSAD.
defm : SKXWriteResPair<WriteMPSADY,  [SKXPort5], 4, [2], 2, 7>;
defm : SKXWriteResPair<WriteMPSADZ,  [SKXPort5], 4, [2], 2, 7>;
defm : SKXWriteResPair<WritePSADBW,  [SKXPort5], 3, [1], 1, 5>; // Vector PSADBW.
defm : SKXWriteResPair<WritePSADBWX, [SKXPort5], 3, [1], 1, 6>;
defm : SKXWriteResPair<WritePSADBWY, [SKXPort5], 3, [1], 1, 7>;
defm : SKXWriteResPair<WritePSADBWZ, [SKXPort5], 3, [1], 1, 7>;
defm : SKXWriteResPair<WritePHMINPOS, [SKXPort0], 4, [1], 1, 6>; // Vector PHMINPOS.

// Vector integer shifts.
defm : SKXWriteResPair<WriteVecShift, [SKXPort0], 1, [1], 1, 5>;
defm : X86WriteRes<WriteVecShiftX,    [SKXPort5,SKXPort01],  2, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftY,    [SKXPort5,SKXPort01],  4, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftZ,    [SKXPort5,SKXPort0],   4, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftXLd,  [SKXPort01,SKXPort23], 7, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftYLd,  [SKXPort01,SKXPort23], 8, [1,1], 2>;
defm : X86WriteRes<WriteVecShiftZLd,  [SKXPort0,SKXPort23],  8, [1,1], 2>;

defm : SKXWriteResPair<WriteVecShiftImm,  [SKXPort0],  1, [1], 1, 5>;
defm : SKXWriteResPair<WriteVecShiftImmX, [SKXPort01], 1, [1], 1, 6>; // Vector integer immediate shifts.
defm : SKXWriteResPair<WriteVecShiftImmY, [SKXPort01], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVecShiftImmZ, [SKXPort0], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVarVecShift,  [SKXPort01], 1, [1], 1, 6>; // Variable vector shifts.
defm : SKXWriteResPair<WriteVarVecShiftY, [SKXPort01], 1, [1], 1, 7>;
defm : SKXWriteResPair<WriteVarVecShiftZ, [SKXPort0], 1, [1], 1, 7>;

// Vector insert/extract operations.
def : WriteRes<WriteVecInsert, [SKXPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def : WriteRes<WriteVecInsertLd, [SKXPort5,SKXPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def: InstRW<[WriteVecInsertLd], (instregex "(V?)MOV(H|L)(PD|PS)rm")>;

def : WriteRes<WriteVecExtract, [SKXPort0,SKXPort5]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def : WriteRes<WriteVecExtractSt, [SKXPort4,SKXPort5,SKXPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

// Conversion between integer and float.
defm : SKXWriteResPair<WriteCvtSS2I,   [SKXPort01], 6, [2], 2>; // Needs more work: DD vs DQ.
defm : SKXWriteResPair<WriteCvtPS2I,   [SKXPort01], 3>;
defm : SKXWriteResPair<WriteCvtPS2IY,  [SKXPort01], 3>;
defm : SKXWriteResPair<WriteCvtPS2IZ,  [SKXPort05], 3>;
defm : SKXWriteResPair<WriteCvtSD2I,   [SKXPort01], 6, [2], 2>;
defm : SKXWriteResPair<WriteCvtPD2I,   [SKXPort01], 3>;
defm : SKXWriteResPair<WriteCvtPD2IY,  [SKXPort01], 3>;
defm : SKXWriteResPair<WriteCvtPD2IZ,  [SKXPort05], 3>;

defm : SKXWriteResPair<WriteCvtI2SS,   [SKXPort1], 4>;
defm : SKXWriteResPair<WriteCvtI2PS,   [SKXPort01], 4>;
defm : SKXWriteResPair<WriteCvtI2PSY,  [SKXPort01], 4>;
defm : SKXWriteResPair<WriteCvtI2PSZ,  [SKXPort05], 4>;  // Needs more work: DD vs DQ.
defm : SKXWriteResPair<WriteCvtI2SD,   [SKXPort1], 4>;
defm : SKXWriteResPair<WriteCvtI2PD,   [SKXPort01], 4>;
defm : SKXWriteResPair<WriteCvtI2PDY,  [SKXPort01], 4>;
defm : SKXWriteResPair<WriteCvtI2PDZ,  [SKXPort05], 4>;

defm : SKXWriteResPair<WriteCvtSS2SD,  [SKXPort1], 3>;
defm : SKXWriteResPair<WriteCvtPS2PD,  [SKXPort1], 3>;
defm : SKXWriteResPair<WriteCvtPS2PDY, [SKXPort5,SKXPort01], 3, [1,1], 2>;
defm : SKXWriteResPair<WriteCvtPS2PDZ, [SKXPort05], 3, [2], 2>;
defm : SKXWriteResPair<WriteCvtSD2SS,  [SKXPort1], 3>;
defm : SKXWriteResPair<WriteCvtPD2PS,  [SKXPort1], 3>;
defm : SKXWriteResPair<WriteCvtPD2PSY, [SKXPort5,SKXPort01], 3, [1,1], 2>;
defm : SKXWriteResPair<WriteCvtPD2PSZ, [SKXPort05], 3, [2], 2>;

defm : X86WriteRes<WriteCvtPH2PS,     [SKXPort5,SKXPort01],  5, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSY,    [SKXPort5,SKXPort01],  7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSZ,    [SKXPort5,SKXPort0],   7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSLd,  [SKXPort23,SKXPort01],  9, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSYLd, [SKXPort23,SKXPort01], 10, [1,1], 2>;
defm : X86WriteRes<WriteCvtPH2PSZLd, [SKXPort23,SKXPort05], 10, [1,1], 2>;

defm : X86WriteRes<WriteCvtPS2PH,    [SKXPort5,SKXPort01], 5, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHY,   [SKXPort5,SKXPort01], 7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHZ,   [SKXPort5,SKXPort05], 7, [1,1], 2>;
defm : X86WriteRes<WriteCvtPS2PHSt,  [SKXPort4,SKXPort5,SKXPort237,SKXPort01], 6, [1,1,1,1], 4>;
defm : X86WriteRes<WriteCvtPS2PHYSt, [SKXPort4,SKXPort5,SKXPort237,SKXPort01], 8, [1,1,1,1], 4>;
defm : X86WriteRes<WriteCvtPS2PHZSt, [SKXPort4,SKXPort5,SKXPort237,SKXPort05], 8, [1,1,1,1], 4>;

// Strings instructions.

// Packed Compare Implicit Length Strings, Return Mask
def : WriteRes<WritePCmpIStrM, [SKXPort0]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def : WriteRes<WritePCmpIStrMLd, [SKXPort0, SKXPort23]> {
  let Latency = 16;
  let NumMicroOps = 4;
  let ResourceCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Mask
def : WriteRes<WritePCmpEStrM, [SKXPort0, SKXPort5, SKXPort015, SKXPort0156]> {
  let Latency = 19;
  let NumMicroOps = 9;
  let ResourceCycles = [4,3,1,1];
}
def : WriteRes<WritePCmpEStrMLd, [SKXPort0, SKXPort5, SKXPort23, SKXPort015, SKXPort0156]> {
  let Latency = 25;
  let NumMicroOps = 10;
  let ResourceCycles = [4,3,1,1,1];
}

// Packed Compare Implicit Length Strings, Return Index
def : WriteRes<WritePCmpIStrI, [SKXPort0]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def : WriteRes<WritePCmpIStrILd, [SKXPort0, SKXPort23]> {
  let Latency = 16;
  let NumMicroOps = 4;
  let ResourceCycles = [3,1];
}

// Packed Compare Explicit Length Strings, Return Index
def : WriteRes<WritePCmpEStrI, [SKXPort0,SKXPort5,SKXPort0156]> {
  let Latency = 18;
  let NumMicroOps = 8;
  let ResourceCycles = [4,3,1];
}
def : WriteRes<WritePCmpEStrILd, [SKXPort0, SKXPort5, SKXPort23, SKXPort0156]> {
  let Latency = 24;
  let NumMicroOps = 9;
  let ResourceCycles = [4,3,1,1];
}

// MOVMSK Instructions.
def : WriteRes<WriteFMOVMSK,    [SKXPort0]> { let Latency = 2; }
def : WriteRes<WriteVecMOVMSK,  [SKXPort0]> { let Latency = 2; }
def : WriteRes<WriteVecMOVMSKY, [SKXPort0]> { let Latency = 2; }
def : WriteRes<WriteMMXMOVMSK,  [SKXPort0]> { let Latency = 2; }

// AES instructions.
def : WriteRes<WriteAESDecEnc, [SKXPort0]> { // Decryption, encryption.
  let Latency = 4;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def : WriteRes<WriteAESDecEncLd, [SKXPort0, SKXPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}

def : WriteRes<WriteAESIMC, [SKXPort0]> { // InvMixColumn.
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def : WriteRes<WriteAESIMCLd, [SKXPort0, SKXPort23]> {
  let Latency = 14;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}

def : WriteRes<WriteAESKeyGen, [SKXPort0,SKXPort5,SKXPort015]> { // Key Generation.
  let Latency = 20;
  let NumMicroOps = 11;
  let ResourceCycles = [3,6,2];
}
def : WriteRes<WriteAESKeyGenLd, [SKXPort0,SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 25;
  let NumMicroOps = 11;
  let ResourceCycles = [3,6,1,1];
}

// Carry-less multiplication instructions.
def : WriteRes<WriteCLMul, [SKXPort5]> {
  let Latency = 6;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def : WriteRes<WriteCLMulLd, [SKXPort5, SKXPort23]> {
  let Latency = 12;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}

// Catch-all for expensive system instructions.
def : WriteRes<WriteSystem,     [SKXPort0156]> { let Latency = 100; } // def WriteSystem : SchedWrite;

// AVX2.
defm : SKXWriteResPair<WriteFShuffle256, [SKXPort5], 3, [1], 1, 7>; // Fp 256-bit width vector shuffles.
defm : SKXWriteResPair<WriteFVarShuffle256, [SKXPort5], 3, [1], 1, 7>; // Fp 256-bit width vector variable shuffles.
defm : SKXWriteResPair<WriteShuffle256, [SKXPort5], 3, [1], 1, 7>;  // 256-bit width vector shuffles.
defm : SKXWriteResPair<WriteVarShuffle256, [SKXPort5], 3, [1], 1, 7>;  // 256-bit width vector variable shuffles.

// Old microcoded instructions that nobody use.
def : WriteRes<WriteMicrocoded, [SKXPort0156]> { let Latency = 100; } // def WriteMicrocoded : SchedWrite;

// Fence instructions.
def : WriteRes<WriteFence,  [SKXPort23, SKXPort4]>;

// Load/store MXCSR.
def : WriteRes<WriteLDMXCSR, [SKXPort0,SKXPort23,SKXPort0156]> { let Latency = 7; let NumMicroOps = 3; let ResourceCycles = [1,1,1]; }
def : WriteRes<WriteSTMXCSR, [SKXPort4,SKXPort5,SKXPort237]> { let Latency = 2; let NumMicroOps = 3; let ResourceCycles = [1,1,1]; }

// Nop, not very useful expect it provides a model for nops!
def : WriteRes<WriteNop, []>;

////////////////////////////////////////////////////////////////////////////////
// Horizontal add/sub  instructions.
////////////////////////////////////////////////////////////////////////////////

defm : SKXWriteResPair<WriteFHAdd,  [SKXPort5,SKXPort015], 6, [2,1], 3, 6>;
defm : SKXWriteResPair<WriteFHAddY, [SKXPort5,SKXPort015], 6, [2,1], 3, 7>;
defm : SKXWriteResPair<WritePHAdd,  [SKXPort5,SKXPort05],  3, [2,1], 3, 5>;
defm : SKXWriteResPair<WritePHAddX, [SKXPort5,SKXPort015], 3, [2,1], 3, 6>;
defm : SKXWriteResPair<WritePHAddY, [SKXPort5,SKXPort015], 3, [2,1], 3, 7>;

// Remaining instrs.

def SKXWriteResGroup1 : SchedWriteRes<[SKXPort0]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup1], (instregex "KAND(B|D|Q|W)rr",
                                            "KANDN(B|D|Q|W)rr",
                                            "KMOV(B|D|Q|W)kk",
                                            "KNOT(B|D|Q|W)rr",
                                            "KOR(B|D|Q|W)rr",
                                            "KXNOR(B|D|Q|W)rr",
                                            "KXOR(B|D|Q|W)rr",
                                            "MMX_PADDS(B|W)irr",
                                            "MMX_PADDUS(B|W)irr",
                                            "MMX_PAVG(B|W)irr",
                                            "MMX_PCMPEQ(B|D|W)irr",
                                            "MMX_PCMPGT(B|D|W)irr",
                                            "MMX_P(MAX|MIN)SWirr",
                                            "MMX_P(MAX|MIN)UBirr",
                                            "MMX_PSUBS(B|W)irr",
                                            "MMX_PSUBUS(B|W)irr",
                                            "VPMOVB2M(Z|Z128|Z256)rr",
                                            "VPMOVD2M(Z|Z128|Z256)rr",
                                            "VPMOVQ2M(Z|Z128|Z256)rr",
                                            "VPMOVW2M(Z|Z128|Z256)rr")>;

def SKXWriteResGroup3 : SchedWriteRes<[SKXPort5]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup3], (instregex "COM(P?)_FST0r",
                                            "KMOV(B|D|Q|W)kr",
                                            "UCOM_F(P?)r")>;

def SKXWriteResGroup4 : SchedWriteRes<[SKXPort6]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup4], (instregex "JMP(16|32|64)r")>;

def SKXWriteResGroup6 : SchedWriteRes<[SKXPort05]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup6], (instrs FINCSTP, FNOP)>;

def SKXWriteResGroup7 : SchedWriteRes<[SKXPort06]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup7], (instrs CDQ, CQO, CLAC, STAC)>;

def SKXWriteResGroup8 : SchedWriteRes<[SKXPort15]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup8], (instregex "ANDN(32|64)rr")>;

def SKXWriteResGroup9 : SchedWriteRes<[SKXPort015]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup9], (instregex "VBLENDMPD(Z128|Z256)rr",
                                            "VBLENDMPS(Z128|Z256)rr",
                                            "VPADD(B|D|Q|W)(Y|Z|Z128|Z256)rr",
                                            "(V?)PADD(B|D|Q|W)rr",
                                            "VPBLENDD(Y?)rri",
                                            "VPBLENDMB(Z128|Z256)rr",
                                            "VPBLENDMD(Z128|Z256)rr",
                                            "VPBLENDMQ(Z128|Z256)rr",
                                            "VPBLENDMW(Z128|Z256)rr",
                                            "VPSUB(B|D|Q|W)(Y|Z|Z128|Z256)rr",
                                            "(V?)PSUB(B|D|Q|W)rr",
                                            "VPTERNLOGD(Z|Z128|Z256)rri",
                                            "VPTERNLOGQ(Z|Z128|Z256)rri")>;

def SKXWriteResGroup10 : SchedWriteRes<[SKXPort0156]> {
  let Latency = 1;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup10], (instrs CBW, CWDE, CDQE,
                                          CMC, STC,
                                          SGDT64m,
                                          SIDT64m,
                                          SMSW16m,
                                          STRm,
                                          SYSCALL)>;

def SKXWriteResGroup11 : SchedWriteRes<[SKXPort4,SKXPort237]> {
  let Latency = 1;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup11], (instrs FBSTPm, VMPTRSTm)>;
def: InstRW<[SKXWriteResGroup11], (instregex "KMOV(B|D|Q|W)mk",
                                             "ST_FP(32|64|80)m")>;

def SKXWriteResGroup13 : SchedWriteRes<[SKXPort5]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[SKXWriteResGroup13], (instrs MMX_MOVQ2DQrr)>;

def SKXWriteResGroup14 : SchedWriteRes<[SKXPort05]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[SKXWriteResGroup14], (instrs FDECSTP,
                                          MMX_MOVDQ2Qrr)>;

def SKXWriteResGroup15 : SchedWriteRes<[SKXPort06]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[SKXWriteResGroup15], (instregex "SET(A|BE)r")>;

def SKXWriteResGroup17 : SchedWriteRes<[SKXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[SKXWriteResGroup17], (instrs LFENCE,
                                          WAIT,
                                          XGETBV)>;

def SKXWriteResGroup20 : SchedWriteRes<[SKXPort6,SKXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup20], (instregex "CLFLUSH")>;

def SKXWriteResGroup21 : SchedWriteRes<[SKXPort237,SKXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup21], (instrs SFENCE)>;

def SKXWriteResGroup23 : SchedWriteRes<[SKXPort06,SKXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup23], (instrs CWD,
                                          JCXZ, JECXZ, JRCXZ,
                                          ADC8i8, SBB8i8)>;
def: InstRW<[SKXWriteResGroup23], (instregex "ADC8ri",
                                             "SBB8ri")>;

def SKXWriteResGroup25 : SchedWriteRes<[SKXPort4,SKXPort6,SKXPort237]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup25], (instrs FNSTCW16m)>;

def SKXWriteResGroup27 : SchedWriteRes<[SKXPort4,SKXPort237,SKXPort15]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup27], (instregex "MOVBE(16|32|64)mr")>;

def SKXWriteResGroup28 : SchedWriteRes<[SKXPort4,SKXPort237,SKXPort0156]> {
  let Latency = 2;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup28], (instrs PUSH16r, PUSH32r, PUSH64r, PUSH64i8,
                                          STOSB, STOSL, STOSQ, STOSW)>;
def: InstRW<[SKXWriteResGroup28], (instregex "PUSH(16|32|64)rmr")>;

def SKXWriteResGroup29 : SchedWriteRes<[SKXPort4,SKXPort237,SKXPort15]> {
  let Latency = 2;
  let NumMicroOps = 5;
  let ResourceCycles = [2,2,1];
}
def: InstRW<[SKXWriteResGroup29], (instregex "VMOVDQU8Zmr(b?)")>;

def SKXWriteResGroup30 : SchedWriteRes<[SKXPort0]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup30], (instregex "KMOV(B|D|Q|W)rk",
                                             "KORTEST(B|D|Q|W)rr",
                                             "KTEST(B|D|Q|W)rr")>;

def SKXWriteResGroup31 : SchedWriteRes<[SKXPort1]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup31], (instregex "PDEP(32|64)rr",
                                             "PEXT(32|64)rr")>;

def SKXWriteResGroup32 : SchedWriteRes<[SKXPort5]> {
  let Latency = 3;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup32], (instrs VPSADBWZrr)>; // TODO: 512-bit ops require ports 0/1 to be joined.
def: InstRW<[SKXWriteResGroup32], (instregex "(ADD|SUB|SUBR)_(FPrST0|FST0r|FrST0)",
                                             "KADD(B|D|Q|W)rr",
                                             "KSHIFTL(B|D|Q|W)ri",
                                             "KSHIFTR(B|D|Q|W)ri",
                                             "KUNPCK(BW|DQ|WD)rr",
                                             "VALIGND(Z|Z128|Z256)rri",
                                             "VALIGNQ(Z|Z128|Z256)rri",
                                             "VCMPPD(Z|Z128|Z256)rri",
                                             "VCMPPS(Z|Z128|Z256)rri",
                                             "VCMP(SD|SS)Zrr",
                                             "VDBPSADBWZrri", // TODO: 512-bit ops require ports 0/1 to be joined.
                                             "VFPCLASS(PD|PS)(Z|Z128|Z256)rr",
                                             "VFPCLASS(SD|SS)Zrr",
                                             "VPBROADCAST(B|W)rr",
                                             "VPCMPB(Z|Z128|Z256)rri",
                                             "VPCMPD(Z|Z128|Z256)rri",
                                             "VPCMPEQ(B|D|Q|W)(Z|Z128|Z256)rr",
                                             "VPCMPGT(B|D|Q|W)(Z|Z128|Z256)rr",
                                             "(V?)PCMPGTQ(Y?)rr",
                                             "VPCMPQ(Z|Z128|Z256)rri",
                                             "VPCMPU(B|D|Q|W)(Z|Z128|Z256)rri",
                                             "VPCMPW(Z|Z128|Z256)rri",
                                             "VP(MAX|MIN)(S|U)Q(Z|Z128|Z256)rr",
                                             "VPTEST(N?)M(B|D|Q|W)(Z|Z128|Z256)rr")>;

def SKXWriteResGroup34 : SchedWriteRes<[SKXPort0,SKXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup34], (instrs FNSTSW16r)>;

def SKXWriteResGroup37 : SchedWriteRes<[SKXPort0,SKXPort5]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SKXWriteResGroup37], (instregex "MMX_PH(ADD|SUB)SWrr")>;

def SKXWriteResGroup38 : SchedWriteRes<[SKXPort5,SKXPort01]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup38], (instregex "(V?)PH(ADD|SUB)SW(Y?)rr")>;

def SKXWriteResGroup41 : SchedWriteRes<[SKXPort5,SKXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup41], (instrs MMX_PACKSSDWirr,
                                          MMX_PACKSSWBirr,
                                          MMX_PACKUSWBirr)>;

def SKXWriteResGroup42 : SchedWriteRes<[SKXPort6,SKXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SKXWriteResGroup42], (instregex "CLD")>;

def SKXWriteResGroup43 : SchedWriteRes<[SKXPort237,SKXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SKXWriteResGroup43], (instrs MFENCE)>;

def SKXWriteResGroup44 : SchedWriteRes<[SKXPort06,SKXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SKXWriteResGroup44], (instregex "RCL(8|16|32|64)r(1|i)",
                                             "RCR(8|16|32|64)r(1|i)")>;

def SKXWriteResGroup45 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort237]> {
  let Latency = 3;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup45], (instrs FNSTSWm)>;

def SKXWriteResGroup46 : SchedWriteRes<[SKXPort4,SKXPort237,SKXPort06]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,2];
}
def: InstRW<[SKXWriteResGroup46], (instregex "SET(A|BE)m")>;

def SKXWriteResGroup47 : SchedWriteRes<[SKXPort4,SKXPort6,SKXPort237,SKXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[SKXWriteResGroup47], (instregex "CALL(16|32|64)r")>;

def SKXWriteResGroup48 : SchedWriteRes<[SKXPort4,SKXPort237,SKXPort06,SKXPort0156]> {
  let Latency = 3;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[SKXWriteResGroup48], (instrs CALL64pcrel32)>;

def SKXWriteResGroup49 : SchedWriteRes<[SKXPort0]> {
  let Latency = 4;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup49], (instregex "MUL_(FPrST0|FST0r|FrST0)")>;

def SKXWriteResGroup50 : SchedWriteRes<[SKXPort01]> {
  let Latency = 4;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup50], (instregex "VCVTDQ2PS(Y|Z128|Z256)rr",
                                             "(V?)CVTDQ2PSrr",
                                             "VCVTPD2QQ(Z128|Z256)rr",
                                             "VCVTPD2UQQ(Z128|Z256)rr",
                                             "VCVTPS2DQ(Y|Z128|Z256)rr",
                                             "(V?)CVTPS2DQrr",
                                             "VCVTPS2UDQ(Z128|Z256)rr",
                                             "VCVTQQ2PD(Z128|Z256)rr",
                                             "VCVTTPD2QQ(Z128|Z256)rr",
                                             "VCVTTPD2UQQ(Z128|Z256)rr",
                                             "VCVTTPS2DQ(Z128|Z256)rr",
                                             "(V?)CVTTPS2DQrr",
                                             "VCVTTPS2UDQ(Z128|Z256)rr",
                                             "VCVTUDQ2PS(Z128|Z256)rr",
                                             "VCVTUQQ2PD(Z128|Z256)rr")>;

def SKXWriteResGroup50z : SchedWriteRes<[SKXPort05]> {
  let Latency = 4;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup50z], (instrs VCVTDQ2PSZrr,
                                           VCVTPD2QQZrr,
                                           VCVTPD2UQQZrr,
                                           VCVTPS2DQZrr,
                                           VCVTPS2UDQZrr,
                                           VCVTQQ2PDZrr,
                                           VCVTTPD2QQZrr,
                                           VCVTTPD2UQQZrr,
                                           VCVTTPS2DQZrr,
                                           VCVTTPS2UDQZrr,
                                           VCVTUDQ2PSZrr,
                                           VCVTUQQ2PDZrr)>;

def SKXWriteResGroup51 : SchedWriteRes<[SKXPort5]> {
  let Latency = 4;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[SKXWriteResGroup51], (instregex "VEXPANDPD(Z|Z128|Z256)rr",
                                             "VEXPANDPS(Z|Z128|Z256)rr",
                                             "VPEXPANDD(Z|Z128|Z256)rr",
                                             "VPEXPANDQ(Z|Z128|Z256)rr",
                                             "VPMOVDB(Z|Z128|Z256)rr",
                                             "VPMOVDW(Z|Z128|Z256)rr",
                                             "VPMOVQB(Z|Z128|Z256)rr",
                                             "VPMOVQW(Z|Z128|Z256)rr",
                                             "VPMOVSDB(Z|Z128|Z256)rr",
                                             "VPMOVSDW(Z|Z128|Z256)rr",
                                             "VPMOVSQB(Z|Z128|Z256)rr",
                                             "VPMOVSQD(Z|Z128|Z256)rr",
                                             "VPMOVSQW(Z|Z128|Z256)rr",
                                             "VPMOVSWB(Z|Z128|Z256)rr",
                                             "VPMOVUSDB(Z|Z128|Z256)rr",
                                             "VPMOVUSDW(Z|Z128|Z256)rr",
                                             "VPMOVUSQB(Z|Z128|Z256)rr",
                                             "VPMOVUSQD(Z|Z128|Z256)rr",
                                             "VPMOVUSWB(Z|Z128|Z256)rr",
                                             "VPMOVWB(Z|Z128|Z256)rr")>;

def SKXWriteResGroup54 : SchedWriteRes<[SKXPort4,SKXPort5,SKXPort237]> {
  let Latency = 4;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup54], (instregex "IST(T?)_FP(16|32|64)m",
                                             "IST_F(16|32)m",
                                             "VPMOVQD(Z|Z128|Z256)mr(b?)")>;

def SKXWriteResGroup55 : SchedWriteRes<[SKXPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ResourceCycles = [4];
}
def: InstRW<[SKXWriteResGroup55], (instrs FNCLEX)>;

def SKXWriteResGroup56 : SchedWriteRes<[]> {
  let Latency = 0;
  let NumMicroOps = 4;
  let ResourceCycles = [];
}
def: InstRW<[SKXWriteResGroup56], (instrs VZEROUPPER)>;

def SKXWriteResGroup57 : SchedWriteRes<[SKXPort1,SKXPort6,SKXPort0156]> {
  let Latency = 4;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,2];
}
def: InstRW<[SKXWriteResGroup57], (instregex "LAR(16|32|64)rr")>;

def SKXWriteResGroup58 : SchedWriteRes<[SKXPort23]> {
  let Latency = 5;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup58], (instregex "MOVSX(16|32|64)rm(8|16|32)",
                                             "MOVZX(16|32|64)rm(8|16)",
                                             "(V?)MOVDDUPrm")>;  // TODO: Should this be SKXWriteResGroup71?

def SKXWriteResGroup61 : SchedWriteRes<[SKXPort5,SKXPort015]> {
  let Latency = 5;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup61], (instregex "MMX_CVT(T?)PD2PIirr",
                                             "MMX_CVT(T?)PS2PIirr",
                                             "VCVTDQ2PDZ128rr",
                                             "VCVTPD2DQZ128rr",
                                             "(V?)CVT(T?)PD2DQrr",
                                             "VCVTPD2PSZ128rr",
                                             "(V?)CVTPD2PSrr",
                                             "VCVTPD2UDQZ128rr",
                                             "VCVTPS2PDZ128rr",
                                             "(V?)CVTPS2PDrr",
                                             "VCVTPS2QQZ128rr",
                                             "VCVTPS2UQQZ128rr",
                                             "VCVTQQ2PSZ128rr",
                                             "(V?)CVTSD2SS(Z?)rr",
                                             "(V?)CVTSI(64)?2SDrr",
                                             "VCVTSI2SSZrr",
                                             "(V?)CVTSI2SSrr",
                                             "VCVTSI(64)?2SDZrr",
                                             "VCVTSS2SDZrr",
                                             "(V?)CVTSS2SDrr",
                                             "VCVTTPD2DQZ128rr",
                                             "VCVTTPD2UDQZ128rr",
                                             "VCVTTPS2QQZ128rr",
                                             "VCVTTPS2UQQZ128rr",
                                             "VCVTUDQ2PDZ128rr",
                                             "VCVTUQQ2PSZ128rr",
                                             "VCVTUSI2SSZrr",
                                             "VCVTUSI(64)?2SDZrr")>;

def SKXWriteResGroup62 : SchedWriteRes<[SKXPort5,SKXPort015]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup62], (instregex "VPCONFLICTQZ128rr")>;

def SKXWriteResGroup63 : SchedWriteRes<[SKXPort1,SKXPort6,SKXPort06]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup63], (instregex "STR(16|32|64)r")>;

def SKXWriteResGroup65 : SchedWriteRes<[SKXPort4,SKXPort237,SKXPort015]> {
  let Latency = 5;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup65], (instregex "VCVTPS2PHZ128mr(b?)",
                                             "VCVTPS2PHZ256mr(b?)",
                                             "VCVTPS2PHZmr(b?)")>;

def SKXWriteResGroup66 : SchedWriteRes<[SKXPort4,SKXPort5,SKXPort237]> {
  let Latency = 5;
  let NumMicroOps = 4;
  let ResourceCycles = [1,2,1];
}
def: InstRW<[SKXWriteResGroup66], (instregex "VPMOVDB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVDW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVQB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVQW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSDB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSDW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSQB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSQD(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSQW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVSWB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSDB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSDW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSQB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSQD(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSQW(Z|Z128|Z256)mr(b?)",
                                             "VPMOVUSWB(Z|Z128|Z256)mr(b?)",
                                             "VPMOVWB(Z|Z128|Z256)mr(b?)")>;

def SKXWriteResGroup67 : SchedWriteRes<[SKXPort06,SKXPort0156]> {
  let Latency = 5;
  let NumMicroOps = 5;
  let ResourceCycles = [1,4];
}
def: InstRW<[SKXWriteResGroup67], (instrs XSETBV)>;

def SKXWriteResGroup69 : SchedWriteRes<[SKXPort4,SKXPort237,SKXPort0156]> {
  let Latency = 5;
  let NumMicroOps = 6;
  let ResourceCycles = [1,1,4];
}
def: InstRW<[SKXWriteResGroup69], (instregex "PUSHF(16|64)")>;

def SKXWriteResGroup71 : SchedWriteRes<[SKXPort23]> {
  let Latency = 6;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup71], (instrs VBROADCASTSSrm,
                                          VPBROADCASTDrm,
                                          VPBROADCASTQrm,
                                          VMOVSHDUPrm,
                                          VMOVSLDUPrm,
                                          MOVSHDUPrm,
                                          MOVSLDUPrm)>;

def SKXWriteResGroup72 : SchedWriteRes<[SKXPort5]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [2];
}
def: InstRW<[SKXWriteResGroup72], (instrs MMX_CVTPI2PSirr)>;
def: InstRW<[SKXWriteResGroup72], (instregex "VCOMPRESSPD(Z|Z128|Z256)rr",
                                             "VCOMPRESSPS(Z|Z128|Z256)rr",
                                             "VPCOMPRESSD(Z|Z128|Z256)rr",
                                             "VPCOMPRESSQ(Z|Z128|Z256)rr",
                                             "VPERMW(Z|Z128|Z256)rr")>;

def SKXWriteResGroup73 : SchedWriteRes<[SKXPort0,SKXPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup73], (instrs MMX_PADDSBirm,
                                          MMX_PADDSWirm,
                                          MMX_PADDUSBirm,
                                          MMX_PADDUSWirm,
                                          MMX_PAVGBirm,
                                          MMX_PAVGWirm,
                                          MMX_PCMPEQBirm,
                                          MMX_PCMPEQDirm,
                                          MMX_PCMPEQWirm,
                                          MMX_PCMPGTBirm,
                                          MMX_PCMPGTDirm,
                                          MMX_PCMPGTWirm,
                                          MMX_PMAXSWirm,
                                          MMX_PMAXUBirm,
                                          MMX_PMINSWirm,
                                          MMX_PMINUBirm,
                                          MMX_PSUBSBirm,
                                          MMX_PSUBSWirm,
                                          MMX_PSUBUSBirm,
                                          MMX_PSUBUSWirm)>;

def SKXWriteResGroup76 : SchedWriteRes<[SKXPort6,SKXPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup76], (instrs FARJMP64)>;
def: InstRW<[SKXWriteResGroup76], (instregex "JMP(16|32|64)m")>;

def SKXWriteResGroup79 : SchedWriteRes<[SKXPort23,SKXPort15]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup79], (instregex "ANDN(32|64)rm",
                                             "MOVBE(16|32|64)rm")>;

def SKXWriteResGroup80 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup80], (instregex "VMOV(64to|QI2)PQIZrm(b?)")>;
def: InstRW<[SKXWriteResGroup80], (instrs VMOVDI2PDIZrm)>;

def SKXWriteResGroup81 : SchedWriteRes<[SKXPort23,SKXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup81], (instrs POP16r, POP32r, POP64r)>;
def: InstRW<[SKXWriteResGroup81], (instregex "POP(16|32|64)rmr")>;

def SKXWriteResGroup82 : SchedWriteRes<[SKXPort5,SKXPort015]> {
  let Latency = 6;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup82], (instregex "(V?)CVTSI642SSrr",
                                             "VCVTSI642SSZrr",
                                             "VCVTUSI642SSZrr")>;

def SKXWriteResGroup84 : SchedWriteRes<[SKXPort1,SKXPort6,SKXPort06,SKXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[SKXWriteResGroup84], (instregex "SLDT(16|32|64)r")>;

def SKXWriteResGroup86 : SchedWriteRes<[SKXPort4,SKXPort23,SKXPort237,SKXPort06]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[SKXWriteResGroup86], (instregex "SAR(8|16|32|64)m(1|i)",
                                             "SHL(8|16|32|64)m(1|i)",
                                             "SHR(8|16|32|64)m(1|i)")>;

def SKXWriteResGroup87 : SchedWriteRes<[SKXPort4,SKXPort23,SKXPort237,SKXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[SKXWriteResGroup87], (instregex "POP(16|32|64)rmm",
                                             "PUSH(16|32|64)rmm")>;

def SKXWriteResGroup88 : SchedWriteRes<[SKXPort6,SKXPort0156]> {
  let Latency = 6;
  let NumMicroOps = 6;
  let ResourceCycles = [1,5];
}
def: InstRW<[SKXWriteResGroup88], (instrs STD)>;

def SKXWriteResGroup89 : SchedWriteRes<[SKXPort23]> {
  let Latency = 7;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup89], (instregex "LD_F(32|64|80)m")>;
def: InstRW<[SKXWriteResGroup89], (instrs VBROADCASTF128,
                                          VBROADCASTI128,
                                          VBROADCASTSDYrm,
                                          VBROADCASTSSYrm,
                                          VMOVDDUPYrm,
                                          VMOVSHDUPYrm,
                                          VMOVSLDUPYrm,
                                          VPBROADCASTDYrm,
                                          VPBROADCASTQYrm)>;

def SKXWriteResGroup90 : SchedWriteRes<[SKXPort01,SKXPort5]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup90], (instrs VCVTDQ2PDYrr)>;

def SKXWriteResGroup92 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup92], (instregex "VMOVSDZrm(b?)",
                                             "VMOVSSZrm(b?)")>;

def SKXWriteResGroup92a : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 6;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup92a], (instregex "(V?)PMOV(SX|ZX)BDrm",
                                              "(V?)PMOV(SX|ZX)BQrm",
                                              "(V?)PMOV(SX|ZX)BWrm",
                                              "(V?)PMOV(SX|ZX)DQrm",
                                              "(V?)PMOV(SX|ZX)WDrm",
                                              "(V?)PMOV(SX|ZX)WQrm")>;

def SKXWriteResGroup93 : SchedWriteRes<[SKXPort5,SKXPort015]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup93], (instregex "VCVTDQ2PDZ256rr",
                                             "VCVTPD2DQ(Y|Z256)rr",
                                             "VCVTPD2PS(Y|Z256)rr",
                                             "VCVTPD2UDQZ256rr",
                                             "VCVTPS2PD(Y|Z256)rr",
                                             "VCVTPS2QQZ256rr",
                                             "VCVTPS2UQQZ256rr",
                                             "VCVTQQ2PSZ256rr",
                                             "VCVTTPD2DQ(Y|Z256)rr",
                                             "VCVTTPD2UDQZ256rr",
                                             "VCVTTPS2QQZ256rr",
                                             "VCVTTPS2UQQZ256rr",
                                             "VCVTUDQ2PDZ256rr",
                                             "VCVTUQQ2PSZ256rr")>;

def SKXWriteResGroup93z : SchedWriteRes<[SKXPort5,SKXPort05]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup93z], (instrs VCVTDQ2PDZrr,
                                           VCVTPD2DQZrr,
                                           VCVTPD2PSZrr,
                                           VCVTPD2UDQZrr,
                                           VCVTPS2PDZrr,
                                           VCVTPS2QQZrr,
                                           VCVTPS2UQQZrr,
                                           VCVTQQ2PSZrr,
                                           VCVTTPD2DQZrr,
                                           VCVTTPD2UDQZrr,
                                           VCVTTPS2QQZrr,
                                           VCVTTPS2UQQZrr,
                                           VCVTUDQ2PDZrr,
                                           VCVTUQQ2PSZrr)>;

def SKXWriteResGroup95 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 7;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup95], (instrs VMOVNTDQAZ128rm,
                                          VPBLENDDrmi)>;
def: InstRW<[SKXWriteResGroup95, ReadAfterVecXLd],
                                  (instregex "VBLENDMPDZ128rm(b?)",
                                             "VBLENDMPSZ128rm(b?)",
                                             "VBROADCASTI32X2Z128m(b?)",
                                             "VBROADCASTSSZ128m(b?)",
                                             "VINSERT(F|I)128rm",
                                             "VMOVAPDZ128rm(b?)",
                                             "VMOVAPSZ128rm(b?)",
                                             "VMOVDDUPZ128rm(b?)",
                                             "VMOVDQA32Z128rm(b?)",
                                             "VMOVDQA64Z128rm(b?)",
                                             "VMOVDQU16Z128rm(b?)",
                                             "VMOVDQU32Z128rm(b?)",
                                             "VMOVDQU64Z128rm(b?)",
                                             "VMOVDQU8Z128rm(b?)",
                                             "VMOVSHDUPZ128rm(b?)",
                                             "VMOVSLDUPZ128rm(b?)",
                                             "VMOVUPDZ128rm(b?)",
                                             "VMOVUPSZ128rm(b?)",
                                             "VPADD(B|D|Q|W)Z128rm(b?)",
                                             "(V?)PADD(B|D|Q|W)rm",
                                             "VPBLENDM(B|D|Q|W)Z128rm(b?)",
                                             "VPBROADCASTDZ128m(b?)",
                                             "VPBROADCASTQZ128m(b?)",
                                             "VPSUB(B|D|Q|W)Z128rm(b?)",
                                             "(V?)PSUB(B|D|Q|W)rm",
                                             "VPTERNLOGDZ128rm(b?)i",
                                             "VPTERNLOGQZ128rm(b?)i")>;

def SKXWriteResGroup96 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup96], (instrs MMX_PACKSSDWirm,
                                          MMX_PACKSSWBirm,
                                          MMX_PACKUSWBirm)>;

def SKXWriteResGroup97 : SchedWriteRes<[SKXPort5,SKXPort015]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup97], (instregex "VPERMI2W128rr",
                                             "VPERMI2W256rr",
                                             "VPERMI2Wrr",
                                             "VPERMT2W128rr",
                                             "VPERMT2W256rr",
                                             "VPERMT2Wrr")>;

def SKXWriteResGroup99 : SchedWriteRes<[SKXPort23,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SKXWriteResGroup99], (instrs LEAVE, LEAVE64,
                                          SCASB, SCASL, SCASQ, SCASW)>;

def SKXWriteResGroup100 : SchedWriteRes<[SKXPort0,SKXPort5,SKXPort015]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup100], (instregex "VCVTSS2USI64Zrr",
                                              "(V?)CVTSS2SI64(Z?)rr",
                                              "(V?)CVTTSS2SI64(Z?)rr",
                                              "VCVTTSS2USI64Zrr")>;

def SKXWriteResGroup101 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort05]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup101], (instrs FLDCW16m)>;

def SKXWriteResGroup103 : SchedWriteRes<[SKXPort5,SKXPort23,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup103], (instregex "KMOV(B|D|Q|W)km")>;

def SKXWriteResGroup104 : SchedWriteRes<[SKXPort6,SKXPort23,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup104], (instrs LRETQ, RETQ)>;

def SKXWriteResGroup106 : SchedWriteRes<[SKXPort4,SKXPort5,SKXPort237]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ResourceCycles = [1,2,1];
}
def: InstRW<[SKXWriteResGroup106], (instregex "VCOMPRESSPD(Z|Z128|Z256)mr(b?)",
                                              "VCOMPRESSPS(Z|Z128|Z256)mr(b?)",
                                              "VPCOMPRESSD(Z|Z128|Z256)mr(b?)",
                                              "VPCOMPRESSQ(Z|Z128|Z256)mr(b?)")>;

def SKXWriteResGroup107 : SchedWriteRes<[SKXPort4,SKXPort23,SKXPort237,SKXPort06]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,2];
}
def: InstRW<[SKXWriteResGroup107], (instregex "ROL(8|16|32|64)m(1|i)",
                                              "ROR(8|16|32|64)m(1|i)")>;

def SKXWriteResGroup108 : SchedWriteRes<[SKXPort4,SKXPort23,SKXPort237,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,2];
}
def: InstRW<[SKXWriteResGroup108], (instregex "XADD(8|16|32|64)rm")>;

def SKXWriteResGroup109 : SchedWriteRes<[SKXPort4,SKXPort6,SKXPort23,SKXPort237,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,1,1];
}
def: InstRW<[SKXWriteResGroup109], (instregex "CALL(16|32|64)m")>;
def: InstRW<[SKXWriteResGroup109], (instrs FARCALL64)>;

def SKXWriteResGroup110 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort237,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 7;
  let ResourceCycles = [1,2,2,2];
}
def: InstRW<[SKXWriteResGroup110], (instrs VPSCATTERDQZ128mr,
                                           VPSCATTERQQZ128mr,
                                           VSCATTERDPDZ128mr,
                                           VSCATTERQPDZ128mr)>;

def SKXWriteResGroup111 : SchedWriteRes<[SKXPort6,SKXPort06,SKXPort15,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 7;
  let ResourceCycles = [1,3,1,2];
}
def: InstRW<[SKXWriteResGroup111], (instrs LOOP)>;

def SKXWriteResGroup112 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort237,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 11;
  let ResourceCycles = [1,4,4,2];
}
def: InstRW<[SKXWriteResGroup112], (instrs VPSCATTERDQZ256mr,
                                           VPSCATTERQQZ256mr,
                                           VSCATTERDPDZ256mr,
                                           VSCATTERQPDZ256mr)>;

def SKXWriteResGroup113 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort237,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 19;
  let ResourceCycles = [1,8,8,2];
}
def: InstRW<[SKXWriteResGroup113], (instrs VPSCATTERDQZmr,
                                           VPSCATTERQQZmr,
                                           VSCATTERDPDZmr,
                                           VSCATTERQPDZmr)>;

def SKXWriteResGroup114 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort5,SKXPort237,SKXPort0156]> {
  let Latency = 7;
  let NumMicroOps = 36;
  let ResourceCycles = [1,16,1,16,2];
}
def: InstRW<[SKXWriteResGroup114], (instrs VSCATTERDPSZmr)>;

def SKXWriteResGroup118 : SchedWriteRes<[SKXPort1,SKXPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup118], (instregex "PDEP(32|64)rm",
                                              "PEXT(32|64)rm")>;

def SKXWriteResGroup119 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup119], (instregex "FCOM(P?)(32|64)m",
                                              "VFPCLASSSDZrm(b?)",
                                              "VPBROADCASTB(Z|Z256)m(b?)",
                                              "VPBROADCASTW(Z|Z256)m(b?)")>;
def: InstRW<[SKXWriteResGroup119], (instrs VPBROADCASTBYrm,
                                           VPBROADCASTWYrm,
                                           VPMOVSXBDYrm,
                                           VPMOVSXBQYrm,
                                           VPMOVSXWQYrm)>;

def SKXWriteResGroup121 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup121], (instrs VMOVNTDQAZ256rm,
                                           VPBLENDDYrmi)>;
def: InstRW<[SKXWriteResGroup121, ReadAfterVecYLd],
                                   (instregex "VBLENDMPD(Z|Z256)rm(b?)",
                                              "VBLENDMPS(Z|Z256)rm(b?)",
                                              "VBROADCASTF32X2Z256m(b?)",
                                              "VBROADCASTF32X2Zm(b?)",
                                              "VBROADCASTF32X4Z256rm(b?)",
                                              "VBROADCASTF32X4rm(b?)",
                                              "VBROADCASTF32X8rm(b?)",
                                              "VBROADCASTF64X2Z128rm(b?)",
                                              "VBROADCASTF64X2rm(b?)",
                                              "VBROADCASTF64X4rm(b?)",
                                              "VBROADCASTI32X2Z256m(b?)",
                                              "VBROADCASTI32X2Zm(b?)",
                                              "VBROADCASTI32X4Z256rm(b?)",
                                              "VBROADCASTI32X4rm(b?)",
                                              "VBROADCASTI32X8rm(b?)",
                                              "VBROADCASTI64X2Z128rm(b?)",
                                              "VBROADCASTI64X2rm(b?)",
                                              "VBROADCASTI64X4rm(b?)",
                                              "VBROADCASTSD(Z|Z256)m(b?)",
                                              "VBROADCASTSS(Z|Z256)m(b?)",
                                              "VINSERTF32x4(Z|Z256)rm(b?)",
                                              "VINSERTF32x8Zrm(b?)",
                                              "VINSERTF64x2(Z|Z256)rm(b?)",
                                              "VINSERTF64x4Zrm(b?)",
                                              "VINSERTI32x4(Z|Z256)rm(b?)",
                                              "VINSERTI32x8Zrm(b?)",
                                              "VINSERTI64x2(Z|Z256)rm(b?)",
                                              "VINSERTI64x4Zrm(b?)",
                                              "VMOVAPD(Z|Z256)rm(b?)",
                                              "VMOVAPS(Z|Z256)rm(b?)",
                                              "VMOVDDUP(Z|Z256)rm(b?)",
                                              "VMOVDQA32(Z|Z256)rm(b?)",
                                              "VMOVDQA64(Z|Z256)rm(b?)",
                                              "VMOVDQU16(Z|Z256)rm(b?)",
                                              "VMOVDQU32(Z|Z256)rm(b?)",
                                              "VMOVDQU64(Z|Z256)rm(b?)",
                                              "VMOVDQU8(Z|Z256)rm(b?)",
                                              "VMOVSHDUP(Z|Z256)rm(b?)",
                                              "VMOVSLDUP(Z|Z256)rm(b?)",
                                              "VMOVUPD(Z|Z256)rm(b?)",
                                              "VMOVUPS(Z|Z256)rm(b?)",
                                              "VPADD(B|D|Q|W)Yrm",
                                              "VPADD(B|D|Q|W)(Z|Z256)rm(b?)",
                                              "VPBLENDM(B|D|Q|W)(Z|Z256)rm(b?)",
                                              "VPBROADCASTD(Z|Z256)m(b?)",
                                              "VPBROADCASTQ(Z|Z256)m(b?)",
                                              "VPSUB(B|D|Q|W)Yrm",
                                              "VPSUB(B|D|Q|W)(Z|Z256)rm(b?)",
                                              "VPTERNLOGD(Z|Z256)rm(b?)i",
                                              "VPTERNLOGQ(Z|Z256)rm(b?)i")>;

def SKXWriteResGroup123 : SchedWriteRes<[SKXPort0,SKXPort5,SKXPort23]> {
  let Latency = 8;
  let NumMicroOps = 4;
  let ResourceCycles = [1,2,1];
}
def: InstRW<[SKXWriteResGroup123], (instregex "MMX_PH(ADD|SUB)SWrm")>;

def SKXWriteResGroup127 : SchedWriteRes<[SKXPort23,SKXPort237,SKXPort06,SKXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 5;
  let ResourceCycles = [1,1,1,2];
}
def: InstRW<[SKXWriteResGroup127], (instregex "RCL(8|16|32|64)m(1|i)",
                                              "RCR(8|16|32|64)m(1|i)")>;

def SKXWriteResGroup128 : SchedWriteRes<[SKXPort4,SKXPort23,SKXPort237,SKXPort06]> {
  let Latency = 8;
  let NumMicroOps = 6;
  let ResourceCycles = [1,1,1,3];
}
def: InstRW<[SKXWriteResGroup128], (instregex "ROL(8|16|32|64)mCL",
                                              "ROR(8|16|32|64)mCL",
                                              "SAR(8|16|32|64)mCL",
                                              "SHL(8|16|32|64)mCL",
                                              "SHR(8|16|32|64)mCL")>;

def SKXWriteResGroup130 : SchedWriteRes<[SKXPort4,SKXPort23,SKXPort237,SKXPort06,SKXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 6;
  let ResourceCycles = [1,1,1,2,1];
}
def: SchedAlias<WriteADCRMW, SKXWriteResGroup130>;

def SKXWriteResGroup131 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort5,SKXPort237,SKXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 8;
  let ResourceCycles = [1,2,1,2,2];
}
def: InstRW<[SKXWriteResGroup131], (instrs VPSCATTERQDZ128mr,
                                           VPSCATTERQDZ256mr,
                                           VSCATTERQPSZ128mr,
                                           VSCATTERQPSZ256mr)>;

def SKXWriteResGroup132 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort5,SKXPort237,SKXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 12;
  let ResourceCycles = [1,4,1,4,2];
}
def: InstRW<[SKXWriteResGroup132], (instrs VPSCATTERDDZ128mr,
                                           VSCATTERDPSZ128mr)>;

def SKXWriteResGroup133 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort5,SKXPort237,SKXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 20;
  let ResourceCycles = [1,8,1,8,2];
}
def: InstRW<[SKXWriteResGroup133], (instrs VPSCATTERDDZ256mr,
                                           VSCATTERDPSZ256mr)>;

def SKXWriteResGroup134 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort5,SKXPort237,SKXPort0156]> {
  let Latency = 8;
  let NumMicroOps = 36;
  let ResourceCycles = [1,16,1,16,2];
}
def: InstRW<[SKXWriteResGroup134], (instrs VPSCATTERDDZmr)>;

def SKXWriteResGroup135 : SchedWriteRes<[SKXPort0,SKXPort23]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup135], (instrs MMX_CVTPI2PSirm)>;

def SKXWriteResGroup136 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup136], (instrs VPMOVSXBWYrm,
                                           VPMOVSXDQYrm,
                                           VPMOVSXWDYrm,
                                           VPMOVZXWDYrm)>;
def: InstRW<[SKXWriteResGroup136], (instregex "VALIGN(D|Q)Z128rm(b?)i",
                                              "VCMP(PD|PS)Z128rm(b?)i",
                                              "VCMP(SD|SS)Zrm",
                                              "VFPCLASSSSZrm(b?)",
                                              "VPCMPBZ128rmi(b?)",
                                              "VPCMPDZ128rmi(b?)",
                                              "VPCMPEQ(B|D|Q|W)Z128rm(b?)",
                                              "VPCMPGT(B|D|Q|W)Z128rm(b?)",
                                              "(V?)PCMPGTQrm",
                                              "VPCMPQZ128rmi(b?)",
                                              "VPCMPU(B|D|Q|W)Z128rmi(b?)",
                                              "VPCMPWZ128rmi(b?)",
                                              "VPERMI2D128rm(b?)",
                                              "VPERMI2PD128rm(b?)",
                                              "VPERMI2PS128rm(b?)",
                                              "VPERMI2Q128rm(b?)",
                                              "VPERMT2D128rm(b?)",
                                              "VPERMT2PD128rm(b?)",
                                              "VPERMT2PS128rm(b?)",
                                              "VPERMT2Q128rm(b?)",
                                              "VPMAXSQZ128rm(b?)",
                                              "VPMAXUQZ128rm(b?)",
                                              "VPMINSQZ128rm(b?)",
                                              "VPMINUQZ128rm(b?)",
                                              "VPMOVSXBDZ128rm(b?)",
                                              "VPMOVSXBQZ128rm(b?)",
                                              "VPMOVSXBWZ128rm(b?)",
                                              "VPMOVSXDQZ128rm(b?)",
                                              "VPMOVSXWDZ128rm(b?)",
                                              "VPMOVSXWQZ128rm(b?)",
                                              "VPMOVZXBDZ128rm(b?)",
                                              "VPMOVZXBQZ128rm(b?)",
                                              "VPMOVZXBWZ128rm(b?)",
                                              "VPMOVZXDQZ128rm(b?)",
                                              "VPMOVZXWDZ128rm(b?)",
                                              "VPMOVZXWQZ128rm(b?)",
                                              "VPTESTMBZ128rm(b?)",
                                              "VPTESTMDZ128rm(b?)",
                                              "VPTESTMQZ128rm(b?)",
                                              "VPTESTMWZ128rm(b?)",
                                              "VPTESTNMBZ128rm(b?)",
                                              "VPTESTNMDZ128rm(b?)",
                                              "VPTESTNMQZ128rm(b?)",
                                              "VPTESTNMWZ128rm(b?)")>;

def SKXWriteResGroup137 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 9;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup137], (instregex "MMX_CVT(T?)PS2PIirm",
                                              "(V?)CVTPS2PDrm")>;

def SKXWriteResGroup143 : SchedWriteRes<[SKXPort5,SKXPort01,SKXPort23]> {
  let Latency = 9;
  let NumMicroOps = 4;
  let ResourceCycles = [2,1,1];
}
def: InstRW<[SKXWriteResGroup143], (instregex "(V?)PHADDSWrm",
                                              "(V?)PHSUBSWrm")>;

def SKXWriteResGroup146 : SchedWriteRes<[SKXPort1,SKXPort6,SKXPort23,SKXPort0156]> {
  let Latency = 9;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup146], (instregex "LAR(16|32|64)rm",
                                              "LSL(16|32|64)rm")>;

def SKXWriteResGroup148 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup148], (instrs VPCMPGTQYrm)>;
def: InstRW<[SKXWriteResGroup148], (instregex "(ADD|SUB|SUBR)_F(32|64)m",
                                              "ILD_F(16|32|64)m",
                                              "VALIGND(Z|Z256)rm(b?)i",
                                              "VALIGNQ(Z|Z256)rm(b?)i",
                                              "VCMPPD(Z|Z256)rm(b?)i",
                                              "VCMPPS(Z|Z256)rm(b?)i",
                                              "VPCMPB(Z|Z256)rmi(b?)",
                                              "VPCMPD(Z|Z256)rmi(b?)",
                                              "VPCMPEQB(Z|Z256)rm(b?)",
                                              "VPCMPEQD(Z|Z256)rm(b?)",
                                              "VPCMPEQQ(Z|Z256)rm(b?)",
                                              "VPCMPEQW(Z|Z256)rm(b?)",
                                              "VPCMPGTB(Z|Z256)rm(b?)",
                                              "VPCMPGTD(Z|Z256)rm(b?)",
                                              "VPCMPGTQ(Z|Z256)rm(b?)",
                                              "VPCMPGTW(Z|Z256)rm(b?)",
                                              "VPCMPQ(Z|Z256)rmi(b?)",
                                              "VPCMPU(B|D|Q|W)Z256rmi(b?)",
                                              "VPCMPU(B|D|Q|W)Zrmi(b?)",
                                              "VPCMPW(Z|Z256)rmi(b?)",
                                              "VPMAXSQ(Z|Z256)rm(b?)",
                                              "VPMAXUQ(Z|Z256)rm(b?)",
                                              "VPMINSQ(Z|Z256)rm(b?)",
                                              "VPMINUQ(Z|Z256)rm(b?)",
                                              "VPTESTM(B|D|Q|W)Z256rm(b?)",
                                              "VPTESTM(B|D|Q|W)Zrm(b?)",
                                              "VPTESTNM(B|D|Q|W)Z256rm(b?)",
                                              "VPTESTNM(B|D|Q|W)Zrm(b?)")>;

def SKXWriteResGroup149 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 10;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup149], (instregex "VCVTDQ2PDZ128rm(b?)",
                                              "VCVTDQ2PSZ128rm(b?)",
                                              "(V?)CVTDQ2PSrm",
                                              "VCVTPD2QQZ128rm(b?)",
                                              "VCVTPD2UQQZ128rm(b?)",
                                              "VCVTPH2PSZ128rm(b?)",
                                              "VCVTPS2DQZ128rm(b?)",
                                              "(V?)CVTPS2DQrm",
                                              "VCVTPS2PDZ128rm(b?)",
                                              "VCVTPS2QQZ128rm(b?)",
                                              "VCVTPS2UDQZ128rm(b?)",
                                              "VCVTPS2UQQZ128rm(b?)",
                                              "VCVTQQ2PDZ128rm(b?)",
                                              "VCVTQQ2PSZ128rm(b?)",
                                              "VCVTSS2SDZrm",
                                              "(V?)CVTSS2SDrm",
                                              "VCVTTPD2QQZ128rm(b?)",
                                              "VCVTTPD2UQQZ128rm(b?)",
                                              "VCVTTPS2DQZ128rm(b?)",
                                              "(V?)CVTTPS2DQrm",
                                              "VCVTTPS2QQZ128rm(b?)",
                                              "VCVTTPS2UDQZ128rm(b?)",
                                              "VCVTTPS2UQQZ128rm(b?)",
                                              "VCVTUDQ2PDZ128rm(b?)",
                                              "VCVTUDQ2PSZ128rm(b?)",
                                              "VCVTUQQ2PDZ128rm(b?)",
                                              "VCVTUQQ2PSZ128rm(b?)")>;

def SKXWriteResGroup151 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup151], (instregex "VEXPANDPDZ128rm(b?)",
                                              "VEXPANDPSZ128rm(b?)",
                                              "VPEXPANDDZ128rm(b?)",
                                              "VPEXPANDQZ128rm(b?)")>;

def SKXWriteResGroup153 : SchedWriteRes<[SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 10;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup153], (instregex "(V?)CVTSD2SSrm")>;

def SKXWriteResGroup154 : SchedWriteRes<[SKXPort5,SKXPort01,SKXPort23]> {
  let Latency = 10;
  let NumMicroOps = 4;
  let ResourceCycles = [2,1,1];
}
def: InstRW<[SKXWriteResGroup154], (instrs VPHADDSWYrm,
                                           VPHSUBSWYrm)>;

def SKXWriteResGroup157 : SchedWriteRes<[SKXPort4,SKXPort6,SKXPort23,SKXPort237,SKXPort06,SKXPort0156]> {
  let Latency = 10;
  let NumMicroOps = 8;
  let ResourceCycles = [1,1,1,1,1,3];
}
def: InstRW<[SKXWriteResGroup157], (instregex "XCHG(8|16|32|64)rm")>;

def SKXWriteResGroup159 : SchedWriteRes<[SKXPort0,SKXFPDivider]> {
  let Latency = 11;
  let NumMicroOps = 1;
  let ResourceCycles = [1,3];
}
def : SchedAlias<WriteFDivX,  SKXWriteResGroup159>; // TODO - convert to ZnWriteResFpuPair

def SKXWriteResGroup160 : SchedWriteRes<[SKXPort0,SKXPort23]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup160], (instregex "MUL_F(32|64)m")>;

def SKXWriteResGroup161 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup161], (instrs VCVTDQ2PSYrm,
                                           VCVTPS2PDYrm)>;
def: InstRW<[SKXWriteResGroup161], (instregex "VCVTDQ2(PD|PS)(Z|Z256)rm(b?)",
                                              "VCVTPH2PS(Z|Z256)rm(b?)",
                                              "VCVTPS2PD(Z|Z256)rm(b?)",
                                              "VCVTQQ2PD(Z|Z256)rm(b?)",
                                              "VCVTQQ2PSZ256rm(b?)",
                                              "VCVT(T?)PD2QQ(Z|Z256)rm(b?)",
                                              "VCVT(T?)PD2UQQ(Z|Z256)rm(b?)",
                                              "VCVT(T?)PS2DQYrm",
                                              "VCVT(T?)PS2DQ(Z|Z256)rm(b?)",
                                              "VCVT(T?)PS2QQZ256rm(b?)",
                                              "VCVT(T?)PS2UDQ(Z|Z256)rm(b?)",
                                              "VCVT(T?)PS2UQQZ256rm(b?)",
                                              "VCVTUDQ2(PD|PS)(Z|Z256)rm(b?)",
                                              "VCVTUQQ2PD(Z|Z256)rm(b?)",
                                              "VCVTUQQ2PSZ256rm(b?)")>;

def SKXWriteResGroup162 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup162], (instregex "FICOM(P?)(16|32)m",
                                              "VEXPANDPD(Z|Z256)rm(b?)",
                                              "VEXPANDPS(Z|Z256)rm(b?)",
                                              "VPEXPANDD(Z|Z256)rm(b?)",
                                              "VPEXPANDQ(Z|Z256)rm(b?)")>;

def SKXWriteResGroup163 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [1,2];
}
def: InstRW<[SKXWriteResGroup163], (instregex "VCVTSD2SSZrm")>;

def SKXWriteResGroup164 : SchedWriteRes<[SKXPort0,SKXPort5,SKXPort23]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup164], (instregex "(V?)CVTDQ2PDrm")>;

def SKXWriteResGroup166 : SchedWriteRes<[SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 11;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup166], (instrs CVTPD2PSrm,
                                           CVTPD2DQrm,
                                           CVTTPD2DQrm,
                                           MMX_CVTPD2PIirm,
                                           MMX_CVTTPD2PIirm)>;

def SKXWriteResGroup167 : SchedWriteRes<[SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 11;
  let NumMicroOps = 4;
  let ResourceCycles = [2,1,1];
}
def: InstRW<[SKXWriteResGroup167], (instregex "VPCONFLICTQZ128rm(b?)")>;

def SKXWriteResGroup169 : SchedWriteRes<[SKXPort1,SKXPort06,SKXPort0156]> {
  let Latency = 11;
  let NumMicroOps = 7;
  let ResourceCycles = [2,3,2];
}
def: InstRW<[SKXWriteResGroup169], (instregex "RCL(16|32|64)rCL",
                                              "RCR(16|32|64)rCL")>;

def SKXWriteResGroup170 : SchedWriteRes<[SKXPort1,SKXPort06,SKXPort15,SKXPort0156]> {
  let Latency = 11;
  let NumMicroOps = 9;
  let ResourceCycles = [1,5,1,2];
}
def: InstRW<[SKXWriteResGroup170], (instrs RCL8rCL)>;

def SKXWriteResGroup171 : SchedWriteRes<[SKXPort06,SKXPort0156]> {
  let Latency = 11;
  let NumMicroOps = 11;
  let ResourceCycles = [2,9];
}
def: InstRW<[SKXWriteResGroup171], (instrs LOOPE, LOOPNE)>;

def SKXWriteResGroup174 : SchedWriteRes<[SKXPort01]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def: InstRW<[SKXWriteResGroup174], (instregex "VPMULLQ(Z128|Z256)rr")>;

def SKXWriteResGroup174z : SchedWriteRes<[SKXPort05]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ResourceCycles = [3];
}
def: InstRW<[SKXWriteResGroup174z], (instregex "VPMULLQZrr")>;

def SKXWriteResGroup175 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup175], (instregex "VPERMWZ128rm(b?)")>;

def SKXWriteResGroup176 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort015]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup176], (instregex "VCVT(T?)SD2USIZrm(b?)",
                                              "VCVT(T?)SS2USI64Zrm(b?)")>;

def SKXWriteResGroup177 : SchedWriteRes<[SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 12;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup177], (instregex "VCVT(T?)PS2QQZrm(b?)",
                                              "VCVT(T?)PS2UQQZrm(b?)")>;

def SKXWriteResGroup179 : SchedWriteRes<[SKXPort0,SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 12;
  let NumMicroOps = 4;
  let ResourceCycles = [1,1,1,1];
}
def: InstRW<[SKXWriteResGroup179], (instregex "CVTTSS2SI64rm")>;

def SKXWriteResGroup180 : SchedWriteRes<[SKXPort5,SKXPort23]> {
  let Latency = 13;
  let NumMicroOps = 3;
  let ResourceCycles = [2,1];
}
def: InstRW<[SKXWriteResGroup180], (instregex "(ADD|SUB|SUBR)_FI(16|32)m",
                                              "VPERMWZ256rm(b?)",
                                              "VPERMWZrm(b?)")>;

def SKXWriteResGroup181 : SchedWriteRes<[SKXPort0,SKXPort5,SKXPort23]> {
  let Latency = 13;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup181], (instrs VCVTDQ2PDYrm)>;

def SKXWriteResGroup183 : SchedWriteRes<[SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 13;
  let NumMicroOps = 4;
  let ResourceCycles = [2,1,1];
}
def: InstRW<[SKXWriteResGroup183], (instregex "VPERMI2W128rm(b?)",
                                              "VPERMT2W128rm(b?)")>;

def SKXWriteResGroup184 : SchedWriteRes<[SKXPort0,SKXFPDivider]> {
  let Latency = 14;
  let NumMicroOps = 1;
  let ResourceCycles = [1,3];
}
def : SchedAlias<WriteFDiv64,  SKXWriteResGroup184>; // TODO - convert to ZnWriteResFpuPair
def : SchedAlias<WriteFDiv64X, SKXWriteResGroup184>; // TODO - convert to ZnWriteResFpuPair

def SKXWriteResGroup184_1 : SchedWriteRes<[SKXPort0,SKXFPDivider]> {
  let Latency = 14;
  let NumMicroOps = 1;
  let ResourceCycles = [1,5];
}
def : SchedAlias<WriteFDiv64Y, SKXWriteResGroup184_1>; // TODO - convert to ZnWriteResFpuPair

def SKXWriteResGroup187 : SchedWriteRes<[SKXPort0,SKXPort5,SKXPort23]> {
  let Latency = 14;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup187], (instregex "MUL_FI(16|32)m")>;

def SKXWriteResGroup188 : SchedWriteRes<[SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 14;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup188], (instregex "VCVTPD2DQZrm(b?)",
                                              "VCVTPD2PSZrm(b?)",
                                              "VCVTPD2UDQZrm(b?)",
                                              "VCVTQQ2PSZrm(b?)",
                                              "VCVTTPD2DQZrm(b?)",
                                              "VCVTTPD2UDQZrm(b?)",
                                              "VCVTUQQ2PSZrm(b?)")>;

def SKXWriteResGroup189 : SchedWriteRes<[SKXPort5,SKXPort23,SKXPort015]> {
  let Latency = 14;
  let NumMicroOps = 4;
  let ResourceCycles = [2,1,1];
}
def: InstRW<[SKXWriteResGroup189], (instregex "VPERMI2W256rm(b?)",
                                              "VPERMI2Wrm(b?)",
                                              "VPERMT2W256rm(b?)",
                                              "VPERMT2Wrm(b?)")>;

def SKXWriteResGroup190 : SchedWriteRes<[SKXPort1,SKXPort06,SKXPort15,SKXPort0156]> {
  let Latency = 14;
  let NumMicroOps = 10;
  let ResourceCycles = [2,4,1,3];
}
def: InstRW<[SKXWriteResGroup190], (instrs RCR8rCL)>;

def SKXWriteResGroup191 : SchedWriteRes<[SKXPort0]> {
  let Latency = 15;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup191], (instregex "DIVR_(FPrST0|FST0r|FrST0)")>;

def SKXWriteResGroup194 : SchedWriteRes<[SKXPort1,SKXPort5,SKXPort01,SKXPort23,SKXPort015]> {
  let Latency = 15;
  let NumMicroOps = 8;
  let ResourceCycles = [1,2,2,1,2];
}
def: InstRW<[SKXWriteResGroup194], (instregex "VPCONFLICTDZ128rm(b?)")>;

def SKXWriteResGroup195 : SchedWriteRes<[SKXPort1,SKXPort23,SKXPort237,SKXPort06,SKXPort15,SKXPort0156]> {
  let Latency = 15;
  let NumMicroOps = 10;
  let ResourceCycles = [1,1,1,5,1,1];
}
def: InstRW<[SKXWriteResGroup195], (instregex "RCL(8|16|32|64)mCL")>;

def SKXWriteResGroup199 : SchedWriteRes<[SKXPort4,SKXPort23,SKXPort237,SKXPort06,SKXPort15,SKXPort0156]> {
  let Latency = 16;
  let NumMicroOps = 14;
  let ResourceCycles = [1,1,1,4,2,5];
}
def: InstRW<[SKXWriteResGroup199], (instrs CMPXCHG8B)>;

def SKXWriteResGroup200 : SchedWriteRes<[SKXPort1, SKXPort05, SKXPort6]> {
  let Latency = 12;
  let NumMicroOps = 34;
  let ResourceCycles = [1, 4, 5];
}
def: InstRW<[SKXWriteResGroup200], (instrs VZEROALL)>;

def SKXWriteResGroup201 : SchedWriteRes<[SKXPort0,SKXPort23,SKXFPDivider]> {
  let Latency = 17;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1,5];
}
def : SchedAlias<WriteFDivXLd, SKXWriteResGroup201>; // TODO - convert to ZnWriteResFpuPair

def SKXWriteResGroup202 : SchedWriteRes<[SKXPort0,SKXPort1,SKXPort5,SKXPort6,SKXPort05,SKXPort0156]> {
  let Latency = 17;
  let NumMicroOps = 15;
  let ResourceCycles = [2,1,2,4,2,4];
}
def: InstRW<[SKXWriteResGroup202], (instrs XCH_F)>;

def SKXWriteResGroup205 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 18;
  let NumMicroOps = 4;
  let ResourceCycles = [1,3];
}
def: InstRW<[SKXWriteResGroup205], (instregex "VPMULLQZ128rm(b?)")>;

def SKXWriteResGroup207 : SchedWriteRes<[SKXPort5,SKXPort6,SKXPort06,SKXPort0156]> {
  let Latency = 18;
  let NumMicroOps = 8;
  let ResourceCycles = [1,1,1,5];
}
def: InstRW<[SKXWriteResGroup207], (instrs CPUID, RDTSC)>;

def SKXWriteResGroup208 : SchedWriteRes<[SKXPort1,SKXPort23,SKXPort237,SKXPort06,SKXPort15,SKXPort0156]> {
  let Latency = 18;
  let NumMicroOps = 11;
  let ResourceCycles = [2,1,1,4,1,2];
}
def: InstRW<[SKXWriteResGroup208], (instregex "RCR(8|16|32|64)mCL")>;

def SKXWriteResGroup209 : SchedWriteRes<[SKXPort0,SKXPort23,SKXFPDivider]> {
  let Latency = 19;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1,4];
}
def : SchedAlias<WriteFDiv64Ld,  SKXWriteResGroup209>; // TODO - convert to ZnWriteResFpuPair

def SKXWriteResGroup211 : SchedWriteRes<[SKXPort23,SKXPort015]> {
  let Latency = 19;
  let NumMicroOps = 4;
  let ResourceCycles = [1,3];
}
def: InstRW<[SKXWriteResGroup211], (instregex "VPMULLQZ256rm(b?)",
                                              "VPMULLQZrm(b?)")>;

def SKXWriteResGroup214 : SchedWriteRes<[]> {
  let Latency = 20;
  let NumMicroOps = 0;
}
def: InstRW<[SKXWriteResGroup214], (instrs VGATHERDPSZ128rm,
                                           VGATHERQPSZrm,
                                           VPGATHERDDZ128rm)>;

def SKXWriteResGroup215 : SchedWriteRes<[SKXPort0]> {
  let Latency = 20;
  let NumMicroOps = 1;
  let ResourceCycles = [1];
}
def: InstRW<[SKXWriteResGroup215], (instregex "DIV_(FPrST0|FST0r|FrST0)")>;

def SKXWriteResGroup216 : SchedWriteRes<[SKXPort0,SKXPort23,SKXFPDivider]> {
  let Latency = 20;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1,4];
}
def : SchedAlias<WriteFDiv64XLd, SKXWriteResGroup216>; // TODO - convert to ZnWriteResFpuPair

def SKXWriteResGroup218 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort015,SKXPort0156]> {
  let Latency = 20;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup218], (instrs VGATHERQPSZ128rm,
                                           VGATHERQPSZ256rm,
                                           VPGATHERQDZ128rm,
                                           VPGATHERQDZ256rm)>;

def SKXWriteResGroup219 : SchedWriteRes<[SKXPort4,SKXPort5,SKXPort6,SKXPort23,SKXPort237,SKXPort06,SKXPort0156]> {
  let Latency = 20;
  let NumMicroOps = 8;
  let ResourceCycles = [1,1,1,1,1,1,2];
}
def: InstRW<[SKXWriteResGroup219], (instrs INSB, INSL, INSW)>;

def SKXWriteResGroup220 : SchedWriteRes<[SKXPort5,SKXPort6,SKXPort0156]> {
  let Latency = 20;
  let NumMicroOps = 10;
  let ResourceCycles = [1,2,7];
}
def: InstRW<[SKXWriteResGroup220], (instrs MWAITrr)>;

def SKXWriteResGroup222 : SchedWriteRes<[SKXPort0,SKXPort23,SKXFPDivider]> {
  let Latency = 21;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1,8];
}
def : SchedAlias<WriteFDiv64YLd, SKXWriteResGroup222>; // TODO - convert to ZnWriteResFpuPair

def SKXWriteResGroup223 : SchedWriteRes<[SKXPort0,SKXPort23]> {
  let Latency = 22;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup223], (instregex "DIV_F(32|64)m")>;

def SKXWriteResGroup224 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort015,SKXPort0156]> {
  let Latency = 22;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup224], (instrs VGATHERDPDZ128rm,
                                           VGATHERQPDZ128rm,
                                           VPGATHERDQZ128rm,
                                           VPGATHERQQZ128rm)>;

def SKXWriteResGroup224_2 : SchedWriteRes<[SKXPort0, SKXPort23, SKXPort5, SKXPort015]> {
  let Latency = 22;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup224_2], (instrs VGATHERDPSrm,
                                             VGATHERDPDrm,
                                             VGATHERQPDrm,
                                             VGATHERQPSrm,
                                             VPGATHERDDrm,
                                             VPGATHERDQrm,
                                             VPGATHERQDrm,
                                             VPGATHERQQrm,
                                             VPGATHERDDrm,
                                             VPGATHERQDrm,
                                             VPGATHERDQrm,
                                             VPGATHERQQrm,
                                             VGATHERDPSrm,
                                             VGATHERQPSrm,
                                             VGATHERDPDrm,
                                             VGATHERQPDrm)>;

def SKXWriteResGroup224_3 : SchedWriteRes<[SKXPort0, SKXPort23, SKXPort5, SKXPort015]> {
  let Latency = 25;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup224_3], (instrs VGATHERDPSYrm,
                                             VGATHERQPDYrm,
                                             VGATHERQPSYrm,
                                             VPGATHERDDYrm,
                                             VPGATHERDQYrm,
                                             VPGATHERQDYrm,
                                             VPGATHERQQYrm,
                                             VPGATHERDDYrm,
                                             VPGATHERQDYrm,
                                             VPGATHERDQYrm,
                                             VPGATHERQQYrm,
                                             VGATHERDPSYrm,
                                             VGATHERQPSYrm,
                                             VGATHERDPDYrm)>;

def SKXWriteResGroup225 : SchedWriteRes<[SKXPort5,SKXPort01,SKXPort015]> {
  let Latency = 22;
  let NumMicroOps = 14;
  let ResourceCycles = [5,5,4];
}
def: InstRW<[SKXWriteResGroup225], (instregex "VPCONFLICTDZ128rr",
                                              "VPCONFLICTQZ256rr")>;

def SKXWriteResGroup228 : SchedWriteRes<[SKXPort0,SKXPort4,SKXPort5,SKXPort23,SKXPort237,SKXPort06,SKXPort0156]> {
  let Latency = 23;
  let NumMicroOps = 19;
  let ResourceCycles = [2,1,4,1,1,4,6];
}
def: InstRW<[SKXWriteResGroup228], (instrs CMPXCHG16B)>;

def SKXWriteResGroup233 : SchedWriteRes<[SKXPort0,SKXPort5,SKXPort23]> {
  let Latency = 25;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup233], (instregex "DIV_FI(16|32)m")>;

def SKXWriteResGroup234 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort015,SKXPort0156]> {
  let Latency = 25;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup234], (instrs VGATHERDPDZ256rm,
                                           VGATHERQPDZ256rm,
                                           VPGATHERDQZ256rm,
                                           VPGATHERQDZrm,
                                           VPGATHERQQZ256rm)>;

def SKXWriteResGroup238 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort015,SKXPort0156]> {
  let Latency = 26;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup238], (instrs VGATHERDPDZrm,
                                           VGATHERQPDZrm,
                                           VPGATHERDQZrm,
                                           VPGATHERQQZrm)>;

def SKXWriteResGroup239 : SchedWriteRes<[SKXPort0,SKXPort23]> {
  let Latency = 27;
  let NumMicroOps = 2;
  let ResourceCycles = [1,1];
}
def: InstRW<[SKXWriteResGroup239], (instregex "DIVR_F(32|64)m")>;

def SKXWriteResGroup240 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort015,SKXPort0156]> {
  let Latency = 27;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup240], (instrs VGATHERDPSZ256rm,
                                           VPGATHERDDZ256rm)>;

def SKXWriteResGroup242 : SchedWriteRes<[SKXPort5,SKXPort01,SKXPort23,SKXPort015]> {
  let Latency = 29;
  let NumMicroOps = 15;
  let ResourceCycles = [5,5,1,4];
}
def: InstRW<[SKXWriteResGroup242], (instregex "VPCONFLICTQZ256rm(b?)")>;

def SKXWriteResGroup243 : SchedWriteRes<[SKXPort0,SKXPort5,SKXPort23]> {
  let Latency = 30;
  let NumMicroOps = 3;
  let ResourceCycles = [1,1,1];
}
def: InstRW<[SKXWriteResGroup243], (instregex "DIVR_FI(16|32)m")>;

def SKXWriteResGroup245 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort015,SKXPort0156]> {
  let Latency = 30;
  let NumMicroOps = 5;
  let ResourceCycles = [1,2,1,1];
}
def: InstRW<[SKXWriteResGroup245], (instrs VGATHERDPSZrm,
                                           VPGATHERDDZrm)>;

def SKXWriteResGroup247 : SchedWriteRes<[SKXPort5,SKXPort6,SKXPort23,SKXPort06,SKXPort0156]> {
  let Latency = 35;
  let NumMicroOps = 23;
  let ResourceCycles = [1,5,3,4,10];
}
def: InstRW<[SKXWriteResGroup247], (instregex "IN(8|16|32)ri",
                                              "IN(8|16|32)rr")>;

def SKXWriteResGroup248 : SchedWriteRes<[SKXPort5,SKXPort6,SKXPort23,SKXPort237,SKXPort06,SKXPort0156]> {
  let Latency = 35;
  let NumMicroOps = 23;
  let ResourceCycles = [1,5,2,1,4,10];
}
def: InstRW<[SKXWriteResGroup248], (instregex "OUT(8|16|32)ir",
                                              "OUT(8|16|32)rr")>;

def SKXWriteResGroup249 : SchedWriteRes<[SKXPort5,SKXPort01,SKXPort015]> {
  let Latency = 37;
  let NumMicroOps = 21;
  let ResourceCycles = [9,7,5];
}
def: InstRW<[SKXWriteResGroup249], (instregex "VPCONFLICTDZ256rr",
                                              "VPCONFLICTQZrr")>;

def SKXWriteResGroup250 : SchedWriteRes<[SKXPort1,SKXPort6,SKXPort23,SKXPort0156]> {
  let Latency = 37;
  let NumMicroOps = 31;
  let ResourceCycles = [1,8,1,21];
}
def: InstRW<[SKXWriteResGroup250], (instregex "XRSTOR(64)?")>;

def SKXWriteResGroup252 : SchedWriteRes<[SKXPort1,SKXPort4,SKXPort5,SKXPort6,SKXPort23,SKXPort237,SKXPort15,SKXPort0156]> {
  let Latency = 40;
  let NumMicroOps = 18;
  let ResourceCycles = [1,1,2,3,1,1,1,8];
}
def: InstRW<[SKXWriteResGroup252], (instrs VMCLEARm)>;

def SKXWriteResGroup253 : SchedWriteRes<[SKXPort4,SKXPort6,SKXPort23,SKXPort237,SKXPort0156]> {
  let Latency = 41;
  let NumMicroOps = 39;
  let ResourceCycles = [1,10,1,1,26];
}
def: InstRW<[SKXWriteResGroup253], (instrs XSAVE64)>;

def SKXWriteResGroup254 : SchedWriteRes<[SKXPort5,SKXPort0156]> {
  let Latency = 42;
  let NumMicroOps = 22;
  let ResourceCycles = [2,20];
}
def: InstRW<[SKXWriteResGroup254], (instrs RDTSCP)>;

def SKXWriteResGroup255 : SchedWriteRes<[SKXPort4,SKXPort6,SKXPort23,SKXPort237,SKXPort0156]> {
  let Latency = 42;
  let NumMicroOps = 40;
  let ResourceCycles = [1,11,1,1,26];
}
def: InstRW<[SKXWriteResGroup255], (instrs XSAVE)>;
def: InstRW<[SKXWriteResGroup255], (instregex "XSAVEC", "XSAVES", "XSAVEOPT")>;

def SKXWriteResGroup256 : SchedWriteRes<[SKXPort5,SKXPort01,SKXPort23,SKXPort015]> {
  let Latency = 44;
  let NumMicroOps = 22;
  let ResourceCycles = [9,7,1,5];
}
def: InstRW<[SKXWriteResGroup256], (instregex "VPCONFLICTDZ256rm(b?)",
                                              "VPCONFLICTQZrm(b?)")>;

def SKXWriteResGroup258 : SchedWriteRes<[SKXPort0,SKXPort23,SKXPort05,SKXPort06,SKXPort0156]> {
  let Latency = 62;
  let NumMicroOps = 64;
  let ResourceCycles = [2,8,5,10,39];
}
def: InstRW<[SKXWriteResGroup258], (instrs FLDENVm)>;

def SKXWriteResGroup259 : SchedWriteRes<[SKXPort0,SKXPort6,SKXPort23,SKXPort05,SKXPort06,SKXPort15,SKXPort0156]> {
  let Latency = 63;
  let NumMicroOps = 88;
  let ResourceCycles = [4,4,31,1,2,1,45];
}
def: InstRW<[SKXWriteResGroup259], (instrs FXRSTOR64)>;

def SKXWriteResGroup260 : SchedWriteRes<[SKXPort0,SKXPort6,SKXPort23,SKXPort05,SKXPort06,SKXPort15,SKXPort0156]> {
  let Latency = 63;
  let NumMicroOps = 90;
  let ResourceCycles = [4,2,33,1,2,1,47];
}
def: InstRW<[SKXWriteResGroup260], (instrs FXRSTOR)>;

def SKXWriteResGroup261 : SchedWriteRes<[SKXPort5,SKXPort01,SKXPort015]> {
  let Latency = 67;
  let NumMicroOps = 35;
  let ResourceCycles = [17,11,7];
}
def: InstRW<[SKXWriteResGroup261], (instregex "VPCONFLICTDZrr")>;

def SKXWriteResGroup262 : SchedWriteRes<[SKXPort5,SKXPort01,SKXPort23,SKXPort015]> {
  let Latency = 74;
  let NumMicroOps = 36;
  let ResourceCycles = [17,11,1,7];
}
def: InstRW<[SKXWriteResGroup262], (instregex "VPCONFLICTDZrm(b?)")>;

def SKXWriteResGroup263 : SchedWriteRes<[SKXPort5,SKXPort05,SKXPort0156]> {
  let Latency = 75;
  let NumMicroOps = 15;
  let ResourceCycles = [6,3,6];
}
def: InstRW<[SKXWriteResGroup263], (instrs FNINIT)>;

def SKXWriteResGroup266 : SchedWriteRes<[SKXPort0,SKXPort1,SKXPort4,SKXPort5,SKXPort6,SKXPort237,SKXPort06,SKXPort0156]> {
  let Latency = 106;
  let NumMicroOps = 100;
  let ResourceCycles = [9,1,11,16,1,11,21,30];
}
def: InstRW<[SKXWriteResGroup266], (instrs FSTENVm)>;

def SKXWriteResGroup267 : SchedWriteRes<[SKXPort6,SKXPort0156]> {
  let Latency = 140;
  let NumMicroOps = 4;
  let ResourceCycles = [1,3];
}
def: InstRW<[SKXWriteResGroup267], (instrs PAUSE)>;

def: InstRW<[WriteZero], (instrs CLC)>;

} // SchedModel
