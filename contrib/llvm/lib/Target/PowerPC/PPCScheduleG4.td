//===-- PPCScheduleG4.td - PPC G4 Scheduling Definitions ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the G4 (7400) processor.
//
//===----------------------------------------------------------------------===//

def G4_BPU    : FuncUnit; // Branch unit
def G4_SLU    : FuncUnit; // Store/load unit
def G4_SRU    : FuncUnit; // special register unit
def G4_IU1    : FuncUnit; // integer unit 1 (simple)
def G4_IU2    : FuncUnit; // integer unit 2 (complex)
def G4_FPU1   : FuncUnit; // floating point unit 1
def G4_VPU    : FuncUnit; // vector permutation unit
def G4_VIU1   : FuncUnit; // vector integer unit 1 (simple)
def G4_VIU2   : FuncUnit; // vector integer unit 2 (complex)
def G4_VFPU   : FuncUnit; // vector floating point unit

def G4Itineraries : ProcessorItineraries<
  [G4_IU1, G4_IU2, G4_SLU, G4_SRU, G4_BPU, G4_FPU1,
   G4_VIU1, G4_VIU2, G4_VPU, G4_VFPU], [], [
  InstrItinData<IIC_IntSimple   , [InstrStage<1, [G4_IU1, G4_IU2]>]>,
  InstrItinData<IIC_IntGeneral  , [InstrStage<1, [G4_IU1, G4_IU2]>]>,
  InstrItinData<IIC_IntCompare  , [InstrStage<1, [G4_IU1, G4_IU2]>]>,
  InstrItinData<IIC_IntDivW     , [InstrStage<19, [G4_IU1]>]>,
  InstrItinData<IIC_IntMFFS     , [InstrStage<3, [G4_FPU1]>]>,
  InstrItinData<IIC_IntMFVSCR   , [InstrStage<1, [G4_VIU1]>]>,
  InstrItinData<IIC_IntMTFSB0   , [InstrStage<3, [G4_FPU1]>]>,
  InstrItinData<IIC_IntMulHW    , [InstrStage<5, [G4_IU1]>]>,
  InstrItinData<IIC_IntMulHWU   , [InstrStage<6, [G4_IU1]>]>,
  InstrItinData<IIC_IntMulLI    , [InstrStage<3, [G4_IU1]>]>,
  InstrItinData<IIC_IntRotate   , [InstrStage<1, [G4_IU1, G4_IU2]>]>,
  InstrItinData<IIC_IntShift    , [InstrStage<1, [G4_IU1, G4_IU2]>]>,
  InstrItinData<IIC_IntTrapW    , [InstrStage<2, [G4_IU1, G4_IU2]>]>,
  InstrItinData<IIC_BrB         , [InstrStage<1, [G4_BPU]>]>,
  InstrItinData<IIC_BrCR        , [InstrStage<1, [G4_SRU]>]>,
  InstrItinData<IIC_BrMCR       , [InstrStage<1, [G4_SRU]>]>,
  InstrItinData<IIC_BrMCRX      , [InstrStage<1, [G4_SRU]>]>,
  InstrItinData<IIC_LdStDCBF    , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStDCBI    , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLoad    , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLoadUpd , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLoadUpdX, [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStStore   , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStSTU     , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStSTUX    , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStDSS     , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStICBI    , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStSTFD    , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStSTFDU   , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLFD     , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLFDU    , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLFDUX   , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLHA     , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLHAU    , [InstrStage<2, [G4_SLU]>]>, 
  InstrItinData<IIC_LdStLHAUX   , [InstrStage<2, [G4_SLU]>]>, 
  InstrItinData<IIC_LdStLMW     , [InstrStage<34, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLVecX   , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStLWARX   , [InstrStage<3, [G4_SLU]>]>,
  InstrItinData<IIC_LdStSTVEBX  , [InstrStage<2, [G4_SLU]>]>,
  InstrItinData<IIC_LdStSTWCX   , [InstrStage<5, [G4_SLU]>]>,
  InstrItinData<IIC_LdStSync    , [InstrStage<8, [G4_SLU]>]>,
  InstrItinData<IIC_SprISYNC    , [InstrStage<2, [G4_SRU]>]>,
  InstrItinData<IIC_SprMFSR     , [InstrStage<3, [G4_SRU]>]>,
  InstrItinData<IIC_SprMTMSR    , [InstrStage<1, [G4_SRU]>]>,
  InstrItinData<IIC_SprMTSR     , [InstrStage<2, [G4_SRU]>]>,
  InstrItinData<IIC_SprTLBSYNC  , [InstrStage<8, [G4_SRU]>]>,
  InstrItinData<IIC_SprMFCR     , [InstrStage<1, [G4_SRU]>]>,
  InstrItinData<IIC_SprMFMSR    , [InstrStage<1, [G4_SRU]>]>,
  InstrItinData<IIC_SprMFSPR    , [InstrStage<3, [G4_SRU]>]>,
  InstrItinData<IIC_SprMFTB     , [InstrStage<1, [G4_SRU]>]>,
  InstrItinData<IIC_SprMTSPR    , [InstrStage<2, [G4_SRU]>]>,
  InstrItinData<IIC_SprMTSRIN   , [InstrStage<2, [G4_SRU]>]>,
  InstrItinData<IIC_SprRFI      , [InstrStage<2, [G4_SRU]>]>,
  InstrItinData<IIC_SprSC       , [InstrStage<2, [G4_SRU]>]>,
  InstrItinData<IIC_FPGeneral   , [InstrStage<1, [G4_FPU1]>]>,
  InstrItinData<IIC_FPAddSub    , [InstrStage<1, [G4_FPU1]>]>,
  InstrItinData<IIC_FPCompare   , [InstrStage<1, [G4_FPU1]>]>,
  InstrItinData<IIC_FPDivD      , [InstrStage<31, [G4_FPU1]>]>,
  InstrItinData<IIC_FPDivS      , [InstrStage<17, [G4_FPU1]>]>,
  InstrItinData<IIC_FPFused     , [InstrStage<1, [G4_FPU1]>]>,
  InstrItinData<IIC_FPRes       , [InstrStage<10, [G4_FPU1]>]>,
  InstrItinData<IIC_VecGeneral  , [InstrStage<1, [G4_VIU1]>]>,
  InstrItinData<IIC_VecFP       , [InstrStage<4, [G4_VFPU]>]>,
  InstrItinData<IIC_VecFPCompare, [InstrStage<1, [G4_VIU1]>]>,
  InstrItinData<IIC_VecComplex  , [InstrStage<3, [G4_VIU2]>]>,
  InstrItinData<IIC_VecPerm     , [InstrStage<1, [G4_VPU]>]>,
  InstrItinData<IIC_VecFPRound  , [InstrStage<4, [G4_VFPU]>]>,
  InstrItinData<IIC_VecVSL      , [InstrStage<1, [G4_VIU1]>]>,
  InstrItinData<IIC_VecVSR      , [InstrStage<1, [G4_VIU1]>]>
]>;
