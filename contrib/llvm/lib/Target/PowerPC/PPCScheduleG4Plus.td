//===-- PPCScheduleG4Plus.td - PPC G4+ Scheduling Defs. ----*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the G4+ (7450) processor.
//
//===----------------------------------------------------------------------===//

def G4P_BPU    : FuncUnit; // Branch unit
def G4P_SLU    : FuncUnit; // Store/load unit
def G4P_SRU    : FuncUnit; // special register unit
def G4P_IU1    : FuncUnit; // integer unit 1 (simple)
def G4P_IU2    : FuncUnit; // integer unit 2 (complex)
def G4P_IU3    : FuncUnit; // integer unit 3 (simple)
def G4P_IU4    : FuncUnit; // integer unit 4 (simple)
def G4P_FPU1   : FuncUnit; // floating point unit 1
def G4P_VPU    : FuncUnit; // vector permutation unit
def G4P_VIU1   : FuncUnit; // vector integer unit 1 (simple)
def G4P_VIU2   : FuncUnit; // vector integer unit 2 (complex)
def G4P_VFPU   : FuncUnit; // vector floating point unit

def G4PlusItineraries : ProcessorItineraries<
  [G4P_IU1, G4P_IU2, G4P_IU3, G4P_IU4, G4P_BPU, G4P_SLU, G4P_FPU1,
   G4P_VFPU, G4P_VIU1, G4P_VIU2, G4P_VPU], [], [
  InstrItinData<IIC_IntSimple   , [InstrStage<1, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_IntGeneral  , [InstrStage<1, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_IntCompare  , [InstrStage<1, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_IntDivW     , [InstrStage<23, [G4P_IU2]>]>,
  InstrItinData<IIC_IntMFFS     , [InstrStage<5, [G4P_FPU1]>]>,
  InstrItinData<IIC_IntMFVSCR   , [InstrStage<2, [G4P_VFPU]>]>,
  InstrItinData<IIC_IntMTFSB0   , [InstrStage<5, [G4P_FPU1]>]>,
  InstrItinData<IIC_IntMulHW    , [InstrStage<4, [G4P_IU2]>]>,
  InstrItinData<IIC_IntMulHWU   , [InstrStage<4, [G4P_IU2]>]>,
  InstrItinData<IIC_IntMulLI    , [InstrStage<3, [G4P_IU2]>]>,
  InstrItinData<IIC_IntRotate   , [InstrStage<1, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_IntShift    , [InstrStage<2, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_IntTrapW    , [InstrStage<2, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_BrB         , [InstrStage<1, [G4P_BPU]>]>,
  InstrItinData<IIC_BrCR        , [InstrStage<2, [G4P_IU2]>]>,
  InstrItinData<IIC_BrMCR       , [InstrStage<2, [G4P_IU2]>]>,
  InstrItinData<IIC_BrMCRX      , [InstrStage<2, [G4P_IU2]>]>,
  InstrItinData<IIC_LdStDCBF    , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStDCBI    , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLoad    , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLoadUpd , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLoadUpdX, [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStStore   , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStDSS     , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStICBI    , [InstrStage<3, [G4P_IU2]>]>,
  InstrItinData<IIC_LdStSTFD    , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStSTFDU   , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLFD     , [InstrStage<4, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLFDU    , [InstrStage<4, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLFDUX   , [InstrStage<4, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLHA     , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLHAU    , [InstrStage<3, [G4P_SLU]>]>,  
  InstrItinData<IIC_LdStLHAUX   , [InstrStage<3, [G4P_SLU]>]>,  
  InstrItinData<IIC_LdStLMW     , [InstrStage<37, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLVecX   , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLWA     , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStLWARX   , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStSTD     , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStSTDCX   , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStSTU     , [InstrStage<3, [G4P_SLU]>]>,  
  InstrItinData<IIC_LdStSTUX    , [InstrStage<3, [G4P_SLU]>]>,  
  InstrItinData<IIC_LdStSTVEBX  , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStSTWCX   , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_LdStSync    , [InstrStage<35, [G4P_SLU]>]>,
  InstrItinData<IIC_SprISYNC    , [InstrStage<0, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_SprMFSR     , [InstrStage<4, [G4P_IU2]>]>,
  InstrItinData<IIC_SprMTMSR    , [InstrStage<2, [G4P_IU2]>]>,
  InstrItinData<IIC_SprMTSR     , [InstrStage<2, [G4P_IU2]>]>,
  InstrItinData<IIC_SprTLBSYNC  , [InstrStage<3, [G4P_SLU]>]>,
  InstrItinData<IIC_SprMFCR     , [InstrStage<2, [G4P_IU2]>]>,
  InstrItinData<IIC_SprMFMSR    , [InstrStage<3, [G4P_IU2]>]>,
  InstrItinData<IIC_SprMFSPR    , [InstrStage<4, [G4P_IU2]>]>,
  InstrItinData<IIC_SprMFTB     , [InstrStage<5, [G4P_IU2]>]>,
  InstrItinData<IIC_SprMTSPR    , [InstrStage<2, [G4P_IU2]>]>,
  InstrItinData<IIC_SprMTSRIN   , [InstrStage<2, [G4P_IU2]>]>,
  InstrItinData<IIC_SprRFI      , [InstrStage<1, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_SprSC       , [InstrStage<0, [G4P_IU1, G4P_IU2,
                                                  G4P_IU3, G4P_IU4]>]>,
  InstrItinData<IIC_FPGeneral   , [InstrStage<5, [G4P_FPU1]>]>,
  InstrItinData<IIC_FPAddSub    , [InstrStage<5, [G4P_FPU1]>]>,  
  InstrItinData<IIC_FPCompare   , [InstrStage<5, [G4P_FPU1]>]>,
  InstrItinData<IIC_FPDivD      , [InstrStage<35, [G4P_FPU1]>]>,
  InstrItinData<IIC_FPDivS      , [InstrStage<21, [G4P_FPU1]>]>,
  InstrItinData<IIC_FPFused     , [InstrStage<5, [G4P_FPU1]>]>,
  InstrItinData<IIC_FPRes       , [InstrStage<14, [G4P_FPU1]>]>,
  InstrItinData<IIC_VecGeneral  , [InstrStage<1, [G4P_VIU1]>]>,
  InstrItinData<IIC_VecFP       , [InstrStage<4, [G4P_VFPU]>]>,
  InstrItinData<IIC_VecFPCompare, [InstrStage<2, [G4P_VFPU]>]>,
  InstrItinData<IIC_VecComplex  , [InstrStage<4, [G4P_VIU2]>]>,
  InstrItinData<IIC_VecPerm     , [InstrStage<2, [G4P_VPU]>]>,
  InstrItinData<IIC_VecFPRound  , [InstrStage<4, [G4P_VIU1]>]>,
  InstrItinData<IIC_VecVSL      , [InstrStage<2, [G4P_VPU]>]>,
  InstrItinData<IIC_VecVSR      , [InstrStage<2, [G4P_VPU]>]>
]>;
