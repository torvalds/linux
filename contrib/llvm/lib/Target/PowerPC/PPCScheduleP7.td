//===-- PPCScheduleP7.td - PPC P7 Scheduling Definitions ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the POWER7 processor.
//
//===----------------------------------------------------------------------===//

// Primary reference:
// IBM POWER7 multicore server processor
// B. Sinharoy, et al.
// IBM J. Res. & Dev. (55) 3. May/June 2011.

// Scheduling for the P7 involves tracking two types of resources:
//  1. The dispatch bundle slots
//  2. The functional unit resources

// Dispatch units:
def P7_DU1    : FuncUnit;
def P7_DU2    : FuncUnit;
def P7_DU3    : FuncUnit;
def P7_DU4    : FuncUnit;
def P7_DU5    : FuncUnit;
def P7_DU6    : FuncUnit;

def P7_LS1    : FuncUnit; // Load/Store pipeline 1
def P7_LS2    : FuncUnit; // Load/Store pipeline 2

def P7_FX1    : FuncUnit; // FX pipeline 1
def P7_FX2    : FuncUnit; // FX pipeline 2

// VS pipeline 1 (vector integer ops. always here)
def P7_VS1    : FuncUnit; // VS pipeline 1
// VS pipeline 2 (128-bit stores and perms. here)
def P7_VS2    : FuncUnit; // VS pipeline 2

def P7_CRU    : FuncUnit; // CR unit (CR logicals and move-from-SPRs)
def P7_BRU    : FuncUnit; // BR unit

// Notes:
// Each LSU pipeline can also execute FX add and logical instructions.
// Each LSU pipeline can complete a load or store in one cycle.
//
// Each store is broken into two parts, AGEN goes to the LSU while a
// "data steering" op. goes to the FXU or VSU.
//
// FX loads have a two cycle load-to-use latency (so one "bubble" cycle).
// VSU loads have a three cycle load-to-use latency (so two "bubble" cycle).
//
// Frequent FX ops. take only one cycle and results can be used again in the
// next cycle (there is a self-bypass). Getting results from the other FX
// pipeline takes an additional cycle.
//
// The VSU XS is similar to the POWER6, but with a pipeline length of 2 cycles
// (instead of 3 cycles on the POWER6). VSU XS handles vector FX-style ops.
// Dispatch of an instruction to VS1 that uses four single prec. inputs
// (either to a float or XC op). prevents dispatch in that cycle to VS2 of any
// floating point instruction.
//
// The VSU PM is similar to the POWER6, but with a pipeline length of 3 cycles
// (instead of 4 cycles on the POWER6). vsel is handled by the PM pipeline
// (unlike on the POWER6).
//
// FMA from the VSUs can forward results in 6 cycles. VS1 XS and vector FP
// share the same write-back, and have a 5-cycle latency difference, so the
// IFU/IDU will not dispatch an XS instructon 5 cycles after a vector FP
// op. has been dispatched to VS1.
//
// Three cycles after an L1 cache hit, a dependent VSU instruction can issue.
//
// Instruction dispatch groups have (at most) four non-branch instructions, and
// two branches. Unlike on the POWER4/5, a branch does not automatically
// end the dispatch group, but a second branch must be the last in the group.

def P7Itineraries : ProcessorItineraries<
  [P7_DU1, P7_DU2, P7_DU3, P7_DU4, P7_DU5, P7_DU6,
   P7_LS1, P7_LS2, P7_FX1, P7_FX2, P7_VS1, P7_VS2, P7_CRU, P7_BRU], [], [
  InstrItinData<IIC_IntSimple   , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2,
                                                  P7_LS1, P7_LS2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_IntGeneral  , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_IntISEL,      [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2], 0>,
                                   InstrStage<1, [P7_BRU]>],
                                  [1, 1, 1, 1]>,
  InstrItinData<IIC_IntCompare  , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [1, 1, 1]>,
  // FIXME: Add record-form itinerary data.
  InstrItinData<IIC_IntDivW     , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<36, [P7_FX1, P7_FX2]>],
                                  [36, 1, 1]>,
  InstrItinData<IIC_IntDivD     , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<68, [P7_FX1, P7_FX2]>],
                                  [68, 1, 1]>,
  InstrItinData<IIC_IntMulHW    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [4, 1, 1]>,
  InstrItinData<IIC_IntMulHWU   , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [4, 1, 1]>,
  InstrItinData<IIC_IntMulHD    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [4, 1, 1]>,
  InstrItinData<IIC_IntMulLI    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [4, 1, 1]>,
  InstrItinData<IIC_IntRotate   , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                   [1, 1, 1]>,
  InstrItinData<IIC_IntRotateD  , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                   [1, 1, 1]>,
  InstrItinData<IIC_IntRotateDI , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                   [1, 1, 1]>,
  InstrItinData<IIC_IntShift    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_IntTrapW    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [1, 1]>,
  InstrItinData<IIC_IntTrapD    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [1, 1]>,
  InstrItinData<IIC_BrB         , [InstrStage<1, [P7_DU5, P7_DU6], 0>,
                                   InstrStage<1, [P7_BRU]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_BrCR        , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_CRU]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_BrMCR       , [InstrStage<1, [P7_DU5, P7_DU6], 0>,
                                   InstrStage<1, [P7_BRU]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_BrMCRX      , [InstrStage<1, [P7_DU5, P7_DU6], 0>,
                                   InstrStage<1, [P7_BRU]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLoad    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_LdStLoadUpd , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [2, 2, 1, 1]>,
  InstrItinData<IIC_LdStLoadUpdX, [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [3, 3, 1, 1]>,
  InstrItinData<IIC_LdStLD      , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_LdStLDU     , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [2, 2, 1, 1]>,
  InstrItinData<IIC_LdStLDUX    , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [3, 3, 1, 1]>,
  InstrItinData<IIC_LdStLFD     , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLVecX   , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLFDU    , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [3, 3, 1, 1]>,
  InstrItinData<IIC_LdStLFDUX   , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [3, 3, 1, 1]>,
  InstrItinData<IIC_LdStLHA     , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLHAU    , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [4, 4, 1, 1]>,
  InstrItinData<IIC_LdStLHAUX   , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [4, 4, 1, 1]>,
  InstrItinData<IIC_LdStLWA     , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLWARX,    [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLDARX,    [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLMW     , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_LdStStore   , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTD     , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTU     , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [2, 1, 1, 1]>,
  InstrItinData<IIC_LdStSTUX    , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [2, 1, 1, 1]>,
  InstrItinData<IIC_LdStSTFD    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTFDU   , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_FX1, P7_FX2], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [2, 1, 1, 1]>,
  InstrItinData<IIC_LdStSTVEBX  , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2], 0>,
                                   InstrStage<1, [P7_VS2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTDCX   , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTWCX   , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_LS1, P7_LS2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_BrMCRX      , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_DU2], 0>,
                                   InstrStage<1, [P7_DU3], 0>,
                                   InstrStage<1, [P7_DU4], 0>,
                                   InstrStage<1, [P7_CRU]>,
                                   InstrStage<1, [P7_FX1, P7_FX2]>],
                                  [3, 1]>, // mtcr
  InstrItinData<IIC_SprMFCR     , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_CRU]>],
                                  [6, 1]>,
  InstrItinData<IIC_SprMFCRF    , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_CRU]>],
                                  [3, 1]>,
  InstrItinData<IIC_SprMTSPR    , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_FX1]>],
                                  [4, 1]>, // mtctr
  InstrItinData<IIC_FPGeneral   , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [5, 1, 1]>,
  InstrItinData<IIC_FPAddSub    , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [5, 1, 1]>,
  InstrItinData<IIC_FPCompare   , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [8, 1, 1]>,
  InstrItinData<IIC_FPDivD      , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [33, 1, 1]>,
  InstrItinData<IIC_FPDivS      , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [27, 1, 1]>,
  InstrItinData<IIC_FPSqrtD     , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [44, 1, 1]>,
  InstrItinData<IIC_FPSqrtS     , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [32, 1, 1]>,
  InstrItinData<IIC_FPFused     , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [5, 1, 1, 1]>,
  InstrItinData<IIC_FPRes       , [InstrStage<1, [P7_DU1, P7_DU2,
                                                  P7_DU3, P7_DU4], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [5, 1, 1]>,
  InstrItinData<IIC_VecGeneral  , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_VS1]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_VecVSL      , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_VS1]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_VecVSR      , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_VS1]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_VecFP       , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [6, 1, 1]>,
  InstrItinData<IIC_VecFPCompare, [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [6, 1, 1]>,
  InstrItinData<IIC_VecFPRound  , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_VS1, P7_VS2]>],
                                  [6, 1, 1]>,
  InstrItinData<IIC_VecComplex  , [InstrStage<1, [P7_DU1], 0>,
                                   InstrStage<1, [P7_VS1]>],
                                  [7, 1, 1]>,
  InstrItinData<IIC_VecPerm     , [InstrStage<1, [P7_DU1, P7_DU2], 0>,
                                   InstrStage<1, [P7_VS2]>],
                                  [3, 1, 1]>
]>;

// ===---------------------------------------------------------------------===//
// P7 machine model for scheduling and other instruction cost heuristics.

def P7Model : SchedMachineModel {
  let IssueWidth = 6;  // 4 (non-branch) instructions are dispatched per cycle.
                       // Note that the dispatch bundle size is 6 (including
                       // branches), but the total internal issue bandwidth per
                       // cycle (from all queues) is 8.

  let LoadLatency = 3; // Optimistic load latency assuming bypass.
                       // This is overriden by OperandCycles if the
                       // Itineraries are queried instead.
  let MispredictPenalty = 16;

  // Try to make sure we have at least 10 dispatch groups in a loop.
  let LoopMicroOpBufferSize = 40;

  let CompleteModel = 0;

  let Itineraries = P7Itineraries;
}

