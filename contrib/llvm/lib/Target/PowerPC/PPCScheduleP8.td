//===-- PPCScheduleP8.td - PPC P8 Scheduling Definitions ---*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the POWER8 processor.
//
//===----------------------------------------------------------------------===//

// Scheduling for the P8 involves tracking two types of resources:
//  1. The dispatch bundle slots
//  2. The functional unit resources

// Dispatch units:
def P8_DU1    : FuncUnit;
def P8_DU2    : FuncUnit;
def P8_DU3    : FuncUnit;
def P8_DU4    : FuncUnit;
def P8_DU5    : FuncUnit;
def P8_DU6    : FuncUnit;
def P8_DU7    : FuncUnit; // Only branch instructions will use DU7,DU8
def P8_DU8    : FuncUnit;

// 10 insns per cycle (2-LU, 2-LSU, 2-FXU, 2-FPU, 1-CRU, 1-BRU).

def P8_LU1     : FuncUnit; // Loads or fixed-point operations 1
def P8_LU2     : FuncUnit; // Loads or fixed-point operations 2

// Load/Store pipelines can handle Stores, fixed-point loads, and simple
// fixed-point operations.
def P8_LSU1    : FuncUnit; // Load/Store pipeline 1
def P8_LSU2    : FuncUnit; // Load/Store pipeline 2

// Fixed Point unit
def P8_FXU1    : FuncUnit; // FX pipeline 1
def P8_FXU2    : FuncUnit; // FX pipeline 2

// The Floating-Point Unit (FPU) and Vector Media Extension (VMX) units
// are combined on P7 and newer into a Vector Scalar Unit (VSU).
// The P8 Instruction latency documents still refers to the unit as the
// FPU, so keep in mind that FPU==VSU.
// In contrast to the P7, the VMX units on P8 are symmetric, so no need to
// split vector integer ops or 128-bit load/store/perms to the specific units.
def P8_FPU1    : FuncUnit; // VS pipeline 1
def P8_FPU2    : FuncUnit; // VS pipeline 2

def P8_CRU    : FuncUnit; // CR unit (CR logicals and move-from-SPRs)
def P8_BRU    : FuncUnit; // BR unit

def P8Itineraries : ProcessorItineraries<
  [P8_DU1, P8_DU2, P8_DU3, P8_DU4, P8_DU5, P8_DU6, P8_DU7, P8_DU8,
   P8_LU1, P8_LU2, P8_LSU1, P8_LSU2, P8_FXU1, P8_FXU2,
   P8_FPU1, P8_FPU2, P8_CRU, P8_BRU], [], [
  InstrItinData<IIC_IntSimple   , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2,
                                                  P8_LU1, P8_LU2,
                                                  P8_LSU1, P8_LSU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_IntGeneral  , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2, P8_LU1,
                                                  P8_LU2, P8_LSU1, P8_LSU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_IntISEL,      [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2], 0>,
                                   InstrStage<1, [P8_BRU]>],
                                  [1, 1, 1, 1]>,
  InstrItinData<IIC_IntCompare  , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_IntDivW     , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<15, [P8_FXU1, P8_FXU2]>],
                                  [15, 1, 1]>,
  InstrItinData<IIC_IntDivD     , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<23, [P8_FXU1, P8_FXU2]>],
                                  [23, 1, 1]>,
  InstrItinData<IIC_IntMulHW    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [4, 1, 1]>,
  InstrItinData<IIC_IntMulHWU   , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [4, 1, 1]>,
  InstrItinData<IIC_IntMulHD    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [4, 1, 1]>,
  InstrItinData<IIC_IntMulLI    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [4, 1, 1]>,
  InstrItinData<IIC_IntRotate   , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                   [1, 1, 1]>,
  InstrItinData<IIC_IntRotateD  , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                   [1, 1, 1]>,
  InstrItinData<IIC_IntRotateDI , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                   [1, 1, 1]>,
  InstrItinData<IIC_IntShift    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_IntTrapW    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [1, 1]>,
  InstrItinData<IIC_IntTrapD    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [1, 1]>,
  InstrItinData<IIC_BrB         , [InstrStage<1, [P8_DU7, P8_DU8], 0>,
                                   InstrStage<1, [P8_BRU]>],
                                  [3, 1, 1]>,
  // FIXME - the Br* groups below are not branch related, so should probably
  // be renamed.
  // IIC_BrCR consists of the cr* instructions.  (crand,crnor,creqv, etc).
  // and should be 'First' in dispatch.
  InstrItinData<IIC_BrCR        , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_CRU]>],
                                  [3, 1, 1]>,
  // IIC_BrMCR consists of the mcrf instruction.
  InstrItinData<IIC_BrMCR       , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_CRU]>],
                                  [3, 1, 1]>,
  // IIC_BrMCRX consists of mcrxr (obsolete instruction) and mtcrf, which
  // should be first in the dispatch group.
  InstrItinData<IIC_BrMCRX      , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_BrMCRX      , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [3, 1]>,
  InstrItinData<IIC_LdStLoad    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_LdStLoadUpd , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2 ], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [2, 2, 1, 1]>,
  // Update-Indexed form loads/stores are no longer first and last in the
  // dispatch group.  They are simply cracked, so require DU1,DU2.
  InstrItinData<IIC_LdStLoadUpdX, [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [3, 3, 1, 1]>,
  InstrItinData<IIC_LdStLD      , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_LdStLDU     , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [2, 2, 1, 1]>,
  InstrItinData<IIC_LdStLDUX    , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [3, 3, 1, 1]>,
  InstrItinData<IIC_LdStLFD     , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LU1, P8_LU2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLVecX   , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LU1, P8_LU2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLFDU    , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LU1, P8_LU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [3, 3, 1, 1]>,
  InstrItinData<IIC_LdStLFDUX   , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LU1, P8_LU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [3, 3, 1, 1]>,
  InstrItinData<IIC_LdStLHA     , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2,
                                                  P8_LU1, P8_LU2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLHAU    , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [4, 4, 1, 1]>,
  // first+last in dispatch group.
  InstrItinData<IIC_LdStLHAUX   , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_DU3], 0>,
                                   InstrStage<1, [P8_DU4], 0>,
                                   InstrStage<1, [P8_DU5], 0>,
                                   InstrStage<1, [P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [4, 4, 1, 1]>,
  InstrItinData<IIC_LdStLWA     , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2]>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLWARX,    [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_DU3], 0>,
                                   InstrStage<1, [P8_DU4], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2]>],
                                  [3, 1, 1]>,
  // first+last
  InstrItinData<IIC_LdStLDARX,    [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_DU3], 0>,
                                   InstrStage<1, [P8_DU4], 0>,
                                   InstrStage<1, [P8_DU5], 0>,
                                   InstrStage<1, [P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2]>],
                                  [3, 1, 1]>,
  InstrItinData<IIC_LdStLMW     , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2,
                                                  P8_LU1, P8_LU2]>],
                                  [2, 1, 1]>,
// Stores are dual-issued from the issue queue, so may only take up one
// dispatch slot.  The instruction will be broken into two IOPS. The agen
// op is issued to the LSU, and the data op (register fetch) is issued
// to either the LU (GPR store) or the VSU (FPR store).
  InstrItinData<IIC_LdStStore   , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2]>,
                                   InstrStage<1, [P8_LU1, P8_LU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTD     , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LU1, P8_LU2,
                                                  P8_LSU1, P8_LSU2]>]
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTU     , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LU1, P8_LU2,
                                                  P8_LSU1, P8_LSU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [2, 1, 1, 1]>,
  // First+last
  InstrItinData<IIC_LdStSTUX    , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_DU3], 0>,
                                   InstrStage<1, [P8_DU4], 0>,
                                   InstrStage<1, [P8_DU5], 0>,
                                   InstrStage<1, [P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [2, 1, 1, 1]>,
  InstrItinData<IIC_LdStSTFD    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTFDU   , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [2, 1, 1, 1]>,
  InstrItinData<IIC_LdStSTVEBX  , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTDCX   , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_DU3], 0>,
                                   InstrStage<1, [P8_DU4], 0>,
                                   InstrStage<1, [P8_DU5], 0>,
                                   InstrStage<1, [P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2], 0>,
                                   InstrStage<1, [P8_LU1, P8_LU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_LdStSTWCX   , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_DU2], 0>,
                                   InstrStage<1, [P8_DU3], 0>,
                                   InstrStage<1, [P8_DU4], 0>,
                                   InstrStage<1, [P8_DU5], 0>,
                                   InstrStage<1, [P8_DU6], 0>,
                                   InstrStage<1, [P8_LSU1, P8_LSU2], 0>,
                                   InstrStage<1, [P8_LU1, P8_LU2]>],
                                  [1, 1, 1]>,
  InstrItinData<IIC_SprMFCR     , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_CRU]>],
                                  [6, 1]>,
  InstrItinData<IIC_SprMFCRF    , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_CRU]>],
                                  [3, 1]>,
  InstrItinData<IIC_SprMTSPR    , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FXU1, P8_FXU2]>],
                                  [4, 1]>, // mtctr
  InstrItinData<IIC_FPGeneral   , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [5, 1, 1]>,
  InstrItinData<IIC_FPAddSub    , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [5, 1, 1]>,
  InstrItinData<IIC_FPCompare   , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [8, 1, 1]>,
  InstrItinData<IIC_FPDivD      , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [33, 1, 1]>,
  InstrItinData<IIC_FPDivS      , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [27, 1, 1]>,
  InstrItinData<IIC_FPSqrtD     , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [44, 1, 1]>,
  InstrItinData<IIC_FPSqrtS     , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [32, 1, 1]>,
  InstrItinData<IIC_FPFused     , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [5, 1, 1, 1]>,
  InstrItinData<IIC_FPRes       , [InstrStage<1, [P8_DU1, P8_DU2, P8_DU3,
                                                  P8_DU4, P8_DU5, P8_DU6], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [5, 1, 1]>,
  InstrItinData<IIC_VecGeneral  , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_VecVSL      , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_VecVSR      , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [2, 1, 1]>,
  InstrItinData<IIC_VecFP       , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [6, 1, 1]>,
  InstrItinData<IIC_VecFPCompare, [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [6, 1, 1]>,
  InstrItinData<IIC_VecFPRound  , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [6, 1, 1]>,
  InstrItinData<IIC_VecComplex  , [InstrStage<1, [P8_DU1], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [7, 1, 1]>,
  InstrItinData<IIC_VecPerm     , [InstrStage<1, [P8_DU1, P8_DU2], 0>,
                                   InstrStage<1, [P8_FPU1, P8_FPU2]>],
                                  [3, 1, 1]>
]>;

// ===---------------------------------------------------------------------===//
// P8 machine model for scheduling and other instruction cost heuristics.
// P8 has an 8 insn dispatch group (6 non-branch, 2 branch) and can issue up
// to 10 insns per cycle (2-LU, 2-LSU, 2-FXU, 2-FPU, 1-CRU, 1-BRU).

def P8Model : SchedMachineModel {
  let IssueWidth = 8;  // up to 8 instructions dispatched per cycle.
                       // up to six non-branch instructions.
                       // up to two branches in a dispatch group.

  let LoadLatency = 3; // Optimistic load latency assuming bypass.
                       // This is overriden by OperandCycles if the
                       // Itineraries are queried instead.
  let MispredictPenalty = 16;

  // Try to make sure we have at least 10 dispatch groups in a loop.
  let LoopMicroOpBufferSize = 60;

  let CompleteModel = 0;

  let Itineraries = P8Itineraries;
}

