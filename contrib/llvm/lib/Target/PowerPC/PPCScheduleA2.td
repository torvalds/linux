//===- PPCScheduleA2.td - PPC A2 Scheduling Definitions --*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

// Primary reference:
// A2 Processor User's Manual.
// IBM (as updated in) 2010.

//===----------------------------------------------------------------------===//
// Functional units on the PowerPC A2 chip sets
//
def A2_XU     : FuncUnit; // A2_XU pipeline
def A2_FU     : FuncUnit; // FI pipeline

//
// This file defines the itinerary class data for the PPC A2 processor.
//
//===----------------------------------------------------------------------===//


def PPCA2Itineraries : ProcessorItineraries<
  [A2_XU, A2_FU], [], [
  InstrItinData<IIC_IntSimple,   [InstrStage<1, [A2_XU]>],
                                 [1, 0, 0]>,
  InstrItinData<IIC_IntGeneral,  [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0]>,
  InstrItinData<IIC_IntISEL,     [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0, 0]>,
  InstrItinData<IIC_IntCompare,  [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0]>,
  InstrItinData<IIC_IntDivW,     [InstrStage<1, [A2_XU]>],
                                 [39, 0, 0]>,
  InstrItinData<IIC_IntDivD,     [InstrStage<1, [A2_XU]>],
                                 [71, 0, 0]>,
  InstrItinData<IIC_IntMulHW,    [InstrStage<1, [A2_XU]>],
                                 [5, 0, 0]>,
  InstrItinData<IIC_IntMulHWU,   [InstrStage<1, [A2_XU]>],
                                 [5, 0, 0]>,
  InstrItinData<IIC_IntMulLI,    [InstrStage<1, [A2_XU]>],
                                 [6, 0, 0]>,
  InstrItinData<IIC_IntRotate,   [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0]>,
  InstrItinData<IIC_IntRotateD,  [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0]>,
  InstrItinData<IIC_IntRotateDI, [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0]>,
  InstrItinData<IIC_IntShift,    [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0]>,
  InstrItinData<IIC_IntTrapW,    [InstrStage<1, [A2_XU]>],
                                 [2, 0]>,
  InstrItinData<IIC_IntTrapD,    [InstrStage<1, [A2_XU]>],
                                 [2, 0]>,
  InstrItinData<IIC_BrB,         [InstrStage<1, [A2_XU]>],
                                 [6, 0, 0]>,
  InstrItinData<IIC_BrCR,        [InstrStage<1, [A2_XU]>],
                                 [1, 0, 0]>,
  InstrItinData<IIC_BrMCR,       [InstrStage<1, [A2_XU]>],
                                 [5, 0, 0]>,
  InstrItinData<IIC_BrMCRX,      [InstrStage<1, [A2_XU]>],
                                 [1, 0, 0]>,
  InstrItinData<IIC_LdStDCBA,    [InstrStage<1, [A2_XU]>],
                                 [1, 0, 0]>,
  InstrItinData<IIC_LdStDCBF,    [InstrStage<1, [A2_XU]>],
                                 [1, 0, 0]>,
  InstrItinData<IIC_LdStDCBI,    [InstrStage<1, [A2_XU]>],
                                 [1, 0, 0]>,
  InstrItinData<IIC_LdStLoad,    [InstrStage<1, [A2_XU]>],
                                 [6, 0, 0]>,
  InstrItinData<IIC_LdStLoadUpd, [InstrStage<1, [A2_XU]>],
                                 [6, 8, 0, 0]>,
  InstrItinData<IIC_LdStLoadUpdX,[InstrStage<1, [A2_XU]>],
                                 [6, 8, 0, 0]>,
  InstrItinData<IIC_LdStLDU,     [InstrStage<1, [A2_XU]>],
                                 [6, 0, 0]>,
  InstrItinData<IIC_LdStLDUX,    [InstrStage<1, [A2_XU]>],
                                 [6, 0, 0]>,
  InstrItinData<IIC_LdStStore,   [InstrStage<1, [A2_XU]>],
                                 [0, 0, 0]>,
  InstrItinData<IIC_LdStICBI,    [InstrStage<1, [A2_XU]>],
                                 [16, 0, 0]>,
  InstrItinData<IIC_LdStSTFD,    [InstrStage<1, [A2_XU]>],
                                 [0, 0, 0]>,
  InstrItinData<IIC_LdStSTFDU,   [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0, 0]>,
  InstrItinData<IIC_LdStLFD,     [InstrStage<1, [A2_XU]>],
                                 [7, 0, 0]>,
  InstrItinData<IIC_LdStLFDU,    [InstrStage<1, [A2_XU]>],
                                 [7, 9, 0, 0]>,
  InstrItinData<IIC_LdStLFDUX,   [InstrStage<1, [A2_XU]>],
                                 [7, 9, 0, 0]>,
  InstrItinData<IIC_LdStLHA,     [InstrStage<1, [A2_XU]>],
                                 [6, 0, 0]>,
  InstrItinData<IIC_LdStLHAU,    [InstrStage<1, [A2_XU]>],
                                 [6, 8, 0, 0]>,
  InstrItinData<IIC_LdStLHAUX,   [InstrStage<1, [A2_XU]>],
                                 [6, 8, 0, 0]>,
  InstrItinData<IIC_LdStLWARX,   [InstrStage<1, [A2_XU]>],
                                 [82, 0, 0]>, // L2 latency
  InstrItinData<IIC_LdStSTD,     [InstrStage<1, [A2_XU]>],
                                 [0, 0, 0]>,
  InstrItinData<IIC_LdStSTU,     [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0, 0]>,
  InstrItinData<IIC_LdStSTUX,    [InstrStage<1, [A2_XU]>],
                                 [2, 0, 0, 0]>,
  InstrItinData<IIC_LdStSTDCX,   [InstrStage<1, [A2_XU]>],
                                 [82, 0, 0]>, // L2 latency
  InstrItinData<IIC_LdStSTWCX,   [InstrStage<1, [A2_XU]>],
                                 [82, 0, 0]>, // L2 latency
  InstrItinData<IIC_LdStSync,    [InstrStage<1, [A2_XU]>],
                                 [6]>,
  InstrItinData<IIC_SprISYNC,    [InstrStage<1, [A2_XU]>],
                                 [16]>,
  InstrItinData<IIC_SprMTMSR,    [InstrStage<1, [A2_XU]>],
                                 [16, 0]>,
  InstrItinData<IIC_SprMFCR,     [InstrStage<1, [A2_XU]>],
                                 [6, 0]>,
  InstrItinData<IIC_SprMFCRF,    [InstrStage<1, [A2_XU]>],
                                 [1, 0]>,
  InstrItinData<IIC_SprMFMSR,    [InstrStage<1, [A2_XU]>],
                                 [4, 0]>,
  InstrItinData<IIC_SprMFSPR,    [InstrStage<1, [A2_XU]>],
                                 [6, 0]>,
  InstrItinData<IIC_SprMFTB,     [InstrStage<1, [A2_XU]>],
                                 [4, 0]>,
  InstrItinData<IIC_SprMTSPR,    [InstrStage<1, [A2_XU]>],
                                 [6, 0]>,
  InstrItinData<IIC_SprRFI,      [InstrStage<1, [A2_XU]>],
                                 [16]>,
  InstrItinData<IIC_SprSC,       [InstrStage<1, [A2_XU]>],
                                 [16]>,
  InstrItinData<IIC_FPGeneral,   [InstrStage<1, [A2_FU]>],
                                 [6, 0, 0]>,
  InstrItinData<IIC_FPAddSub,    [InstrStage<1, [A2_FU]>],
                                 [6, 0, 0]>,
  InstrItinData<IIC_FPCompare,   [InstrStage<1, [A2_FU]>],
                                 [5, 0, 0]>,
  InstrItinData<IIC_FPDivD,      [InstrStage<1, [A2_FU]>],
                                 [72, 0, 0]>,
  InstrItinData<IIC_FPDivS,      [InstrStage<1, [A2_FU]>],
                                 [59, 0, 0]>,
  InstrItinData<IIC_FPSqrtD,     [InstrStage<1, [A2_FU]>],
                                 [69, 0, 0]>,
  InstrItinData<IIC_FPSqrtS,     [InstrStage<1, [A2_FU]>],
                                 [65, 0, 0]>,
  InstrItinData<IIC_FPFused,     [InstrStage<1, [A2_FU]>],
                                 [6, 0, 0, 0]>,
  InstrItinData<IIC_FPRes,       [InstrStage<1, [A2_FU]>],
                                 [6, 0]>
]>;

// ===---------------------------------------------------------------------===//
// A2 machine model for scheduling and other instruction cost heuristics.

def PPCA2Model : SchedMachineModel {
  let IssueWidth = 1;  // 1 instruction is dispatched per cycle.
  let LoadLatency = 6; // Optimistic load latency assuming bypass.
                       // This is overriden by OperandCycles if the
                       // Itineraries are queried instead.
  let MispredictPenalty = 13;

  let CompleteModel = 0;

  let Itineraries = PPCA2Itineraries;
}

