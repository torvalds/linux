//===-- PPCScheduleP9.td - PPC P9 Scheduling Definitions ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the POWER9 processor.
//
//===----------------------------------------------------------------------===//
include "PPCInstrInfo.td"

def P9Model : SchedMachineModel {
  // The maximum number of instructions to be issued at the same time.
  // While a value of 8 is technically correct since 8 instructions can be
  // fetched from the instruction cache. However, only 6 instructions may be
  // actually dispatched at a time.
  let IssueWidth = 8;

  // Load latency is 4 or 5 cycles depending on the load. This latency assumes
  // that we have a cache hit. For a cache miss the load latency will be more.
  // There are two instructions (lxvl, lxvll) that have a latencty of 6 cycles.
  // However it is not worth bumping this value up to 6 when the vast majority
  // of instructions are 4 or 5 cycles.
  let LoadLatency = 5;

  // A total of 16 cycles to recover from a branch mispredict.
  let MispredictPenalty = 16;

  // Try to make sure we have at least 10 dispatch groups in a loop.
  // A dispatch group is 6 instructions.
  let LoopMicroOpBufferSize = 60;

  // As iops are dispatched to a slice, they are held in an independent slice
  // issue queue until all register sources and other dependencies have been
  // resolved and they can be issued. Each of four execution slices has an
  // 11-entry iop issue queue.
  let MicroOpBufferSize = 44;

  let CompleteModel = 1;

  // Do not support QPX (Quad Processing eXtension) or SPE (Signal Procesing
  // Engine) on Power 9.
  let UnsupportedFeatures = [HasQPX, HasSPE];

}

let SchedModel = P9Model in {

  // ***************** Processor Resources *****************

  //Dispatcher:
  def DISPATCHER : ProcResource<12>;

  // Issue Ports
  // An instruction can go down one of two issue queues.
  // Address Generation (AGEN) mainly for loads and stores.
  // Execution (EXEC) for most other instructions.
  // Some instructions cannot be run on just any issue queue and may require an
  // Even or an Odd queue. The EXECE represents the even queues and the EXECO
  // represents the odd queues.
  def IP_AGEN : ProcResource<4>;
  def IP_EXEC : ProcResource<4>;
  def IP_EXECE : ProcResource<2> {
    //Even Exec Ports
    let Super = IP_EXEC;
  }
  def IP_EXECO : ProcResource<2> {
    //Odd Exec Ports
    let Super = IP_EXEC;
  }

  // Pipeline Groups
  // Four ALU (Fixed Point Arithmetic) units in total. Two even, two Odd.
  def ALU : ProcResource<4>;
  def ALUE : ProcResource<2> {
    //Even ALU pipelines
    let Super = ALU;
  }
  def ALUO : ProcResource<2> {
    //Odd ALU pipelines
    let Super = ALU;
  }

  // Two DIV (Fixed Point Divide) units.
  def DIV : ProcResource<2>;

  // Four DP (Floating Point) units in total. Two even, two Odd.
  def DP : ProcResource<4>;
  def DPE : ProcResource<2> {
    //Even DP pipelines
    let Super = DP;
  }
  def DPO : ProcResource<2> {
    //Odd DP pipelines
    let Super = DP;
  }

  // Four LS (Load or Store) units.
  def LS : ProcResource<4>;

  // Two PM (Permute) units.
  def PM : ProcResource<2>;

  // Only one DFU (Decimal Floating Point and Quad Precision) unit.
  def DFU : ProcResource<1>;

  // Only one Branch unit.
  def BR : ProcResource<1> {
    let BufferSize = 16;
  }

  // Only one CY (Crypto) unit.
  def CY : ProcResource<1>;

  // ***************** SchedWriteRes Definitions *****************

  //Dispatcher
  def DISP_1C : SchedWriteRes<[DISPATCHER]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  // Issue Ports
  def IP_AGEN_1C : SchedWriteRes<[IP_AGEN]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  def IP_EXEC_1C : SchedWriteRes<[IP_EXEC]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  def IP_EXECE_1C : SchedWriteRes<[IP_EXECE]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  def IP_EXECO_1C : SchedWriteRes<[IP_EXECO]> {
    let NumMicroOps = 0;
    let Latency = 1;
  }

  //Pipeline Groups

  // ALU Units
  // An ALU may take either 2 or 3 cycles to complete the operation.
  // However, the ALU unit is only every busy for 1 cycle at a time and may
  // receive new instructions each cycle.
  def P9_ALU_2C : SchedWriteRes<[ALU]> {
    let Latency = 2;
  }

  def P9_ALUE_2C : SchedWriteRes<[ALUE]> {
    let Latency = 2;
  }

  def P9_ALUO_2C : SchedWriteRes<[ALUO]> {
    let Latency = 2;
  }

  def P9_ALU_3C : SchedWriteRes<[ALU]> {
    let Latency = 3;
  }

  def P9_ALUE_3C : SchedWriteRes<[ALUE]> {
    let Latency = 3;
  }

  def P9_ALUO_3C : SchedWriteRes<[ALUO]> {
    let Latency = 3;
  }

  // DIV Unit
  // A DIV unit may take from 5 to 40 cycles to complete.
  // Some DIV operations may keep the unit busy for up to 8 cycles.
  def P9_DIV_5C : SchedWriteRes<[DIV]> {
    let Latency = 5;
  }

  def P9_DIV_12C : SchedWriteRes<[DIV]> {
    let Latency = 12;
  }

  def P9_DIV_16C_8 : SchedWriteRes<[DIV]> {
    let ResourceCycles = [8];
    let Latency = 16;
  }

  def P9_DIV_24C_8 : SchedWriteRes<[DIV]> {
    let ResourceCycles = [8];
    let Latency = 24;
  }

  def P9_DIV_40C_8 : SchedWriteRes<[DIV]> {
    let ResourceCycles = [8];
    let Latency = 40;
  }

  // DP Unit
  // A DP unit may take from 2 to 36 cycles to complete.
  // Some DP operations keep the unit busy for up to 10 cycles.
  def P9_DP_2C : SchedWriteRes<[DP]> {
    let Latency = 2;
  }

  def P9_DP_5C : SchedWriteRes<[DP]> {
    let Latency = 5;
  }

  def P9_DP_7C : SchedWriteRes<[DP]> {
    let Latency = 7;
  }

  def P9_DPE_7C : SchedWriteRes<[DPE]> {
    let Latency = 7;
  }

  def P9_DPO_7C : SchedWriteRes<[DPO]> {
    let Latency = 7;
  }

  def P9_DP_22C_5 : SchedWriteRes<[DP]> {
    let ResourceCycles = [5];
    let Latency = 22;
  }

  def P9_DP_24C_8 : SchedWriteRes<[DP]> {
    let ResourceCycles = [8];
    let Latency = 24;
  }

  def P9_DPO_24C_8 : SchedWriteRes<[DPO]> {
    let ResourceCycles = [8];
    let Latency = 24;
  }

  def P9_DPE_24C_8 : SchedWriteRes<[DPE]> {
    let ResourceCycles = [8];
    let Latency = 24;
  }

  def P9_DP_26C_5 : SchedWriteRes<[DP]> {
    let ResourceCycles = [5];
    let Latency = 22;
  }

  def P9_DP_27C_7 : SchedWriteRes<[DP]> {
    let ResourceCycles = [7];
    let Latency = 27;
  }

  def P9_DPE_27C_10 : SchedWriteRes<[DP]> {
    let ResourceCycles = [10];
    let Latency = 27;
  }

  def P9_DPO_27C_10 : SchedWriteRes<[DP]> {
    let ResourceCycles = [10];
    let Latency = 27;
  }

  def P9_DP_33C_8 : SchedWriteRes<[DP]> {
    let ResourceCycles = [8];
    let Latency = 33;
  }

  def P9_DPE_33C_8 : SchedWriteRes<[DPE]> {
    let ResourceCycles = [8];
    let Latency = 33;
  }

  def P9_DPO_33C_8 : SchedWriteRes<[DPO]> {
    let ResourceCycles = [8];
    let Latency = 33;
  }

  def P9_DP_36C_10 : SchedWriteRes<[DP]> {
    let ResourceCycles = [10];
    let Latency = 36;
  }

  def P9_DPE_36C_10 : SchedWriteRes<[DP]> {
    let ResourceCycles = [10];
    let Latency = 36;
  }

  def P9_DPO_36C_10 : SchedWriteRes<[DP]> {
    let ResourceCycles = [10];
    let Latency = 36;
  }

  // PM Unit
  // Three cycle permute operations.
  def P9_PM_3C : SchedWriteRes<[PM]> {
    let Latency = 3;
  }

  // Load and Store Units
  // Loads can have 4, 5 or 6 cycles of latency.
  // Stores are listed as having a single cycle of latency. This is not
  // completely accurate since it takes more than 1 cycle to actually store
  // the value. However, since the store does not produce a result it can be
  // considered complete after one cycle.
  def P9_LS_1C : SchedWriteRes<[LS]> {
    let Latency = 1;
  }

  def P9_LS_4C : SchedWriteRes<[LS]> {
    let Latency = 4;
  }

  def P9_LS_5C : SchedWriteRes<[LS]> {
    let Latency = 5;
  }

  def P9_LS_6C : SchedWriteRes<[LS]> {
    let Latency = 6;
  }

  // DFU Unit
  // Some of the most expensive ops use the DFU.
  // Can take from 12 cycles to 76 cycles to obtain a result.
  // The unit may be busy for up to 62 cycles.
  def P9_DFU_12C : SchedWriteRes<[DFU]> {
    let Latency = 12;
  }

  def P9_DFU_23C : SchedWriteRes<[DFU]> {
    let Latency = 23;
    let ResourceCycles = [11];
  }

  def P9_DFU_24C : SchedWriteRes<[DFU]> {
    let Latency = 24;
    let ResourceCycles = [12];
  }

  def P9_DFU_37C : SchedWriteRes<[DFU]> {
    let Latency = 37;
    let ResourceCycles = [25];
  }

  def P9_DFU_58C : SchedWriteRes<[DFU]> {
    let Latency = 58;
    let ResourceCycles = [44];
  }

  def P9_DFU_76C : SchedWriteRes<[DFU]> {
    let Latency = 76;
    let ResourceCycles = [62];
  }

  // 2 or 5 cycle latencies for the branch unit.
  def P9_BR_2C : SchedWriteRes<[BR]> {
    let Latency = 2;
  }

  def P9_BR_5C : SchedWriteRes<[BR]> {
    let Latency = 5;
  }

  // 6 cycle latency for the crypto unit
  def P9_CY_6C : SchedWriteRes<[CY]> {
    let Latency = 6;
  }

  // ***************** WriteSeq Definitions *****************

  // These are combinations of the resources listed above.
  // The idea is that some cracked instructions cannot be done in parallel and
  // so the latencies for their resources must be added.
  def P9_LoadAndALUOp_6C : WriteSequence<[P9_LS_4C, P9_ALU_2C]>;
  def P9_LoadAndALUOp_7C : WriteSequence<[P9_LS_5C, P9_ALU_2C]>;
  def P9_LoadAndALU2Op_7C : WriteSequence<[P9_LS_4C, P9_ALU_3C]>;
  def P9_LoadAndALU2Op_8C : WriteSequence<[P9_LS_5C, P9_ALU_3C]>;
  def P9_LoadAndPMOp_8C : WriteSequence<[P9_LS_5C, P9_PM_3C]>;
  def P9_LoadAndLoadOp_8C : WriteSequence<[P9_LS_4C, P9_LS_4C]>;
  def P9_IntDivAndALUOp_18C_8 : WriteSequence<[P9_DIV_16C_8, P9_ALU_2C]>;
  def P9_IntDivAndALUOp_26C_8 : WriteSequence<[P9_DIV_24C_8, P9_ALU_2C]>;
  def P9_IntDivAndALUOp_42C_8 : WriteSequence<[P9_DIV_40C_8, P9_ALU_2C]>;
  def P9_StoreAndALUOp_3C : WriteSequence<[P9_LS_1C, P9_ALU_2C]>;
  def P9_StoreAndALUOp_4C : WriteSequence<[P9_LS_1C, P9_ALU_3C]>;
  def P9_ALUOpAndALUOp_4C : WriteSequence<[P9_ALU_2C, P9_ALU_2C]>;
  def P9_ALU2OpAndALU2Op_6C : WriteSequence<[P9_ALU_3C, P9_ALU_3C]>;
  def P9_ALUOpAndALUOpAndALUOp_6C :
    WriteSequence<[P9_ALU_2C, P9_ALU_2C, P9_ALU_2C]>;
  def P9_DPOpAndALUOp_7C : WriteSequence<[P9_DP_5C, P9_ALU_2C]>;
  def P9_DPOpAndALUOp_9C : WriteSequence<[P9_DP_7C, P9_ALU_2C]>;
  def P9_DPOpAndALU2Op_10C : WriteSequence<[P9_DP_7C, P9_ALU_3C]>;
  def P9_DPOpAndALUOp_24C_5 : WriteSequence<[P9_DP_22C_5, P9_ALU_2C]>;
  def P9_DPOpAndALUOp_35C_8 : WriteSequence<[P9_DP_33C_8, P9_ALU_2C]>;
  def P9_DPOpAndALU2Op_25C_5 : WriteSequence<[P9_DP_22C_5, P9_ALU_3C]>;
  def P9_DPOpAndALU2Op_29C_5 : WriteSequence<[P9_DP_26C_5, P9_ALU_3C]>;
  def P9_DPOpAndALU2Op_36C_8 : WriteSequence<[P9_DP_33C_8, P9_ALU_3C]>;
  def P9_DPOpAndALU2Op_39C_10 : WriteSequence<[P9_DP_36C_10, P9_ALU_3C]>;
  def P9_BROpAndALUOp_7C : WriteSequence<[P9_BR_5C, P9_ALU_2C]>;

  // Include the resource requirements of individual instructions.
  include "P9InstrResources.td"

}

