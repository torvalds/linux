//===-- PPCScheduleG5.td - PPC G5 Scheduling Definitions ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the G5 (970) processor.
//
//===----------------------------------------------------------------------===//

def G5_BPU    : FuncUnit; // Branch unit
def G5_SLU    : FuncUnit; // Store/load unit
def G5_SRU    : FuncUnit; // special register unit
def G5_IU1    : FuncUnit; // integer unit 1 (simple)
def G5_IU2    : FuncUnit; // integer unit 2 (complex)
def G5_FPU1   : FuncUnit; // floating point unit 1
def G5_FPU2   : FuncUnit; // floating point unit 2
def G5_VPU    : FuncUnit; // vector permutation unit
def G5_VIU1   : FuncUnit; // vector integer unit 1 (simple)
def G5_VIU2   : FuncUnit; // vector integer unit 2 (complex)
def G5_VFPU   : FuncUnit; // vector floating point unit

def G5Itineraries : ProcessorItineraries<
  [G5_IU1, G5_IU2, G5_SLU, G5_BPU, G5_FPU1, G5_FPU2,
   G5_VFPU, G5_VIU1, G5_VIU2, G5_VPU], [], [
  InstrItinData<IIC_IntSimple   , [InstrStage<2, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntGeneral  , [InstrStage<2, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntCompare  , [InstrStage<3, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntDivD     , [InstrStage<68, [G5_IU1]>]>,
  InstrItinData<IIC_IntDivW     , [InstrStage<36, [G5_IU1]>]>,
  InstrItinData<IIC_IntMFFS     , [InstrStage<6, [G5_IU2]>]>,
  InstrItinData<IIC_IntMFVSCR   , [InstrStage<1, [G5_VFPU]>]>,
  InstrItinData<IIC_IntMTFSB0   , [InstrStage<6, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_IntMulHD    , [InstrStage<7, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntMulHW    , [InstrStage<5, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntMulHWU   , [InstrStage<5, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntMulLI    , [InstrStage<4, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntRFID     , [InstrStage<1, [G5_IU2]>]>,
  InstrItinData<IIC_IntRotateD  , [InstrStage<2, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntRotateDI , [InstrStage<2, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntRotate   , [InstrStage<4, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntShift    , [InstrStage<2, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntTrapD    , [InstrStage<1, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_IntTrapW    , [InstrStage<1, [G5_IU1, G5_IU2]>]>,
  InstrItinData<IIC_BrB         , [InstrStage<1, [G5_BPU]>]>,
  InstrItinData<IIC_BrCR        , [InstrStage<4, [G5_BPU]>]>,
  InstrItinData<IIC_BrMCR       , [InstrStage<2, [G5_BPU]>]>,
  InstrItinData<IIC_BrMCRX      , [InstrStage<3, [G5_BPU]>]>,
  InstrItinData<IIC_LdStDCBF    , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLoad    , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLoadUpd , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLoadUpdX, [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStStore   , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStDSS     , [InstrStage<10, [G5_SLU]>]>,
  InstrItinData<IIC_LdStICBI    , [InstrStage<40, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSTFD    , [InstrStage<4, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSTFDU   , [InstrStage<4, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLD      , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLDU     , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLDUX    , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLDARX   , [InstrStage<11, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLFD     , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLFDU    , [InstrStage<5, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLFDUX   , [InstrStage<5, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLHA     , [InstrStage<5, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLHAU    , [InstrStage<5, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLHAUX   , [InstrStage<5, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLMW     , [InstrStage<64, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLVecX   , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLWA     , [InstrStage<5, [G5_SLU]>]>,
  InstrItinData<IIC_LdStLWARX   , [InstrStage<11, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSLBIA   , [InstrStage<40, [G5_SLU]>]>, // needs work
  InstrItinData<IIC_LdStSLBIE   , [InstrStage<2, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSTD     , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSTU     , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSTUX    , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSTDCX   , [InstrStage<11, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSTVEBX  , [InstrStage<5, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSTWCX   , [InstrStage<11, [G5_SLU]>]>,
  InstrItinData<IIC_LdStSync    , [InstrStage<35, [G5_SLU]>]>,
  InstrItinData<IIC_SprISYNC    , [InstrStage<40, [G5_SLU]>]>, // needs work
  InstrItinData<IIC_SprMFSR     , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_SprMTMSR    , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_SprMTSR     , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_SprTLBSYNC  , [InstrStage<3, [G5_SLU]>]>,
  InstrItinData<IIC_SprMFCR     , [InstrStage<2, [G5_IU2]>]>,
  InstrItinData<IIC_SprMFCRF    , [InstrStage<2, [G5_IU2]>]>,
  InstrItinData<IIC_SprMFMSR    , [InstrStage<3, [G5_IU2]>]>,
  InstrItinData<IIC_SprMFSPR    , [InstrStage<3, [G5_IU2]>]>,
  InstrItinData<IIC_SprMFTB     , [InstrStage<10, [G5_IU2]>]>,
  InstrItinData<IIC_SprMTSPR    , [InstrStage<8, [G5_IU2]>]>,
  InstrItinData<IIC_SprSC       , [InstrStage<1, [G5_IU2]>]>,
  InstrItinData<IIC_FPGeneral   , [InstrStage<6, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_FPAddSub    , [InstrStage<6, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_FPCompare   , [InstrStage<8, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_FPDivD      , [InstrStage<33, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_FPDivS      , [InstrStage<33, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_FPFused     , [InstrStage<6, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_FPRes       , [InstrStage<6, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_FPSqrtD     , [InstrStage<40, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_FPSqrtS     , [InstrStage<40, [G5_FPU1, G5_FPU2]>]>,
  InstrItinData<IIC_VecGeneral  , [InstrStage<2, [G5_VIU1]>]>,
  InstrItinData<IIC_VecFP       , [InstrStage<8, [G5_VFPU]>]>,
  InstrItinData<IIC_VecFPCompare, [InstrStage<2, [G5_VFPU]>]>,
  InstrItinData<IIC_VecComplex  , [InstrStage<5, [G5_VIU2]>]>,
  InstrItinData<IIC_VecPerm     , [InstrStage<3, [G5_VPU]>]>,
  InstrItinData<IIC_VecFPRound  , [InstrStage<8, [G5_VFPU]>]>,
  InstrItinData<IIC_VecVSL      , [InstrStage<2, [G5_VIU1]>]>,
  InstrItinData<IIC_VecVSR      , [InstrStage<3, [G5_VPU]>]>
]>;

// ===---------------------------------------------------------------------===//
// G5 machine model for scheduling and other instruction cost heuristics.

def G5Model : SchedMachineModel {
  let IssueWidth = 4;  // 4 (non-branch) instructions are dispatched per cycle.
  let LoadLatency = 3; // Optimistic load latency assuming bypass.
                       // This is overriden by OperandCycles if the
                       // Itineraries are queried instead.
  let MispredictPenalty = 16;

  let CompleteModel = 0;

  let Itineraries = G5Itineraries;
}

