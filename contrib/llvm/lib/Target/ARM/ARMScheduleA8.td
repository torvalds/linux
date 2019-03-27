//=- ARMScheduleA8.td - ARM Cortex-A8 Scheduling Definitions -*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the itinerary class data for the ARM Cortex A8 processors.
//
//===----------------------------------------------------------------------===//

//
// Scheduling information derived from "Cortex-A8 Technical Reference Manual".
// Functional Units.
def A8_Pipe0   : FuncUnit; // pipeline 0
def A8_Pipe1   : FuncUnit; // pipeline 1
def A8_LSPipe  : FuncUnit; // Load / store pipeline
def A8_NPipe   : FuncUnit; // NEON ALU/MUL pipe
def A8_NLSPipe : FuncUnit; // NEON LS pipe
//
// Dual issue pipeline represented by A8_Pipe0 | A8_Pipe1
//
def CortexA8Itineraries : ProcessorItineraries<
  [A8_Pipe0, A8_Pipe1, A8_LSPipe, A8_NPipe, A8_NLSPipe],
  [], [
  // Two fully-pipelined integer ALU pipelines
  //
  // No operand cycles
  InstrItinData<IIC_iALUx    , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>]>,
  //
  // Binary Instructions that produce a result
  InstrItinData<IIC_iALUi ,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2]>,
  InstrItinData<IIC_iALUr ,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2, 2]>,
  InstrItinData<IIC_iALUsi,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2, 1]>,
  InstrItinData<IIC_iALUsir,[InstrStage<1,[A8_Pipe0, A8_Pipe1]>], [2, 1, 2]>,
  InstrItinData<IIC_iALUsr,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2, 1, 1]>,
  //
  // Bitwise Instructions that produce a result
  InstrItinData<IIC_iBITi ,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2]>,
  InstrItinData<IIC_iBITr ,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2, 2]>,
  InstrItinData<IIC_iBITsi,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2, 1]>,
  InstrItinData<IIC_iBITsr,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2, 1, 1]>,
  //
  // Unary Instructions that produce a result
  InstrItinData<IIC_iUNAr , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2]>,
  InstrItinData<IIC_iUNAsi, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 1]>,
  //
  // Zero and sign extension instructions
  InstrItinData<IIC_iEXTr , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1, 1]>,
  InstrItinData<IIC_iEXTAr, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2, 1]>,
  InstrItinData<IIC_iEXTAsr,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>],[2, 2, 1, 1]>,
  //
  // Compare instructions
  InstrItinData<IIC_iCMPi , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2]>,
  InstrItinData<IIC_iCMPr , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2]>,
  InstrItinData<IIC_iCMPsi, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 1]>,
  InstrItinData<IIC_iCMPsr, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 1, 1]>,
  //
  // Test instructions
  InstrItinData<IIC_iTSTi , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2]>,
  InstrItinData<IIC_iTSTr , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2]>,
  InstrItinData<IIC_iTSTsi, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 1]>,
  InstrItinData<IIC_iTSTsr, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 1, 1]>,
  //
  // Move instructions, unconditional
  InstrItinData<IIC_iMOVi , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1]>,
  InstrItinData<IIC_iMOVr , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1, 1]>,
  InstrItinData<IIC_iMOVsi, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1, 1]>,
  InstrItinData<IIC_iMOVsr, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1, 1, 1]>,
  InstrItinData<IIC_iMOVix2,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                             InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2]>,
  InstrItinData<IIC_iMOVix2addpc,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                                  InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                                  InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [3]>,
  InstrItinData<IIC_iMOVix2ld,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<1, [A8_LSPipe]>], [5]>,
  //
  // Move instructions, conditional
  InstrItinData<IIC_iCMOVi , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2]>,
  InstrItinData<IIC_iCMOVr , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 1]>,
  InstrItinData<IIC_iCMOVsi, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 1]>,
  InstrItinData<IIC_iCMOVsr, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 1, 1]>,
  InstrItinData<IIC_iCMOVix2,[InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                              InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [3, 1]>,
  //
  // MVN instructions
  InstrItinData<IIC_iMVNi , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1]>,
  InstrItinData<IIC_iMVNr , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1, 1]>,
  InstrItinData<IIC_iMVNsi, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1, 1]>,
  InstrItinData<IIC_iMVNsr, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [1, 1, 1]>,

  // Integer multiply pipeline
  // Result written in E5, but that is relative to the last cycle of multicycle,
  // so we use 6 for those cases
  //
  InstrItinData<IIC_iMUL16   , [InstrStage<1, [A8_Pipe0]>], [5, 1, 1]>,
  InstrItinData<IIC_iMAC16   , [InstrStage<2, [A8_Pipe0]>], [6, 1, 1, 4]>,
  InstrItinData<IIC_iMUL32   , [InstrStage<2, [A8_Pipe0]>], [6, 1, 1]>,
  InstrItinData<IIC_iMAC32   , [InstrStage<2, [A8_Pipe0]>], [6, 1, 1, 4]>,
  InstrItinData<IIC_iMUL64   , [InstrStage<3, [A8_Pipe0]>], [6, 6, 1, 1]>,
  InstrItinData<IIC_iMAC64   , [InstrStage<3, [A8_Pipe0]>], [6, 6, 1, 1]>,

  // Integer load pipeline
  //
  // Immediate offset
  InstrItinData<IIC_iLoad_i   , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1]>,
  InstrItinData<IIC_iLoad_bh_i, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1]>,
  InstrItinData<IIC_iLoad_d_i,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1]>,
  //
  // Register offset
  InstrItinData<IIC_iLoad_r   , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1, 1]>,
  InstrItinData<IIC_iLoad_bh_r, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1, 1]>,
  InstrItinData<IIC_iLoad_d_r , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1, 1]>,
  //
  // Scaled register offset, issues over 2 cycles
  // FIXME: lsl by 2 takes 1 cycle.
  InstrItinData<IIC_iLoad_si  , [InstrStage<2, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [4, 1, 1]>,
  InstrItinData<IIC_iLoad_bh_si,[InstrStage<2, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [4, 1, 1]>,
  //
  // Immediate offset with update
  InstrItinData<IIC_iLoad_iu  , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 2, 1]>,
  InstrItinData<IIC_iLoad_bh_iu,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 2, 1]>,
  //
  // Register offset with update
  InstrItinData<IIC_iLoad_ru  , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 2, 1, 1]>,
  InstrItinData<IIC_iLoad_bh_ru,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 2, 1, 1]>,
  InstrItinData<IIC_iLoad_d_ru, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 2, 1, 1]>,
  //
  // Scaled register offset with update, issues over 2 cycles
  InstrItinData<IIC_iLoad_siu , [InstrStage<2, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<2, [A8_LSPipe]>], [4, 3, 1, 1]>,
  InstrItinData<IIC_iLoad_bh_siu,[InstrStage<2, [A8_Pipe0, A8_Pipe1], 0>,
                                  InstrStage<2, [A8_LSPipe]>], [4, 3, 1, 1]>,
  //
  // Load multiple, def is the 5th operand. Pipeline 0 only.
  // FIXME: A8_LSPipe cycle time is dynamic, this assumes 3 to 4 registers.
  InstrItinData<IIC_iLoad_m  , [InstrStage<2, [A8_Pipe0], 0>,
                                InstrStage<2, [A8_LSPipe]>],
                [1, 1, 1, 1, 3], [], -1>, // dynamic uops
  //
  // Load multiple + update, defs are the 1st and 5th operands.
  InstrItinData<IIC_iLoad_mu , [InstrStage<3, [A8_Pipe0], 0>,
                                InstrStage<3, [A8_LSPipe]>],
                [2, 1, 1, 1, 3], [], -1>, // dynamic uops
  //
  // Load multiple plus branch
  InstrItinData<IIC_iLoad_mBr, [InstrStage<3, [A8_Pipe0], 0>,
                                InstrStage<3, [A8_LSPipe]>,
                                InstrStage<1, [A8_Pipe0, A8_Pipe1]>],
                              [1, 2, 1, 1, 3], [], -1>, // dynamic uops
  //
  // Pop, def is the 3rd operand.
  InstrItinData<IIC_iPop  ,    [InstrStage<3, [A8_Pipe0], 0>,
                                InstrStage<3, [A8_LSPipe]>],
                [1, 1, 3], [], -1>, // dynamic uops
  //
  // Push, def is the 3th operand.
  InstrItinData<IIC_iPop_Br,   [InstrStage<3, [A8_Pipe0], 0>,
                                InstrStage<3, [A8_LSPipe]>,
                                InstrStage<1, [A8_Pipe0, A8_Pipe1]>],
                               [1, 1, 3], [], -1>, // dynamic uops
  //
  // iLoadi + iALUr for t2LDRpci_pic.
  InstrItinData<IIC_iLoadiALU, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                InstrStage<1, [A8_LSPipe]>,
                                InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [4, 1]>,


  // Integer store pipeline
  //
  // Immediate offset
  InstrItinData<IIC_iStore_i  , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1]>,
  InstrItinData<IIC_iStore_bh_i,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1]>,
  InstrItinData<IIC_iStore_d_i, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1]>,
  //
  // Register offset
  InstrItinData<IIC_iStore_r  , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1, 1]>,
  InstrItinData<IIC_iStore_bh_r,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1, 1]>,
  InstrItinData<IIC_iStore_d_r, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [3, 1, 1]>,
  //
  // Scaled register offset, issues over 2 cycles
  InstrItinData<IIC_iStore_si , [InstrStage<2, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<2, [A8_LSPipe]>], [3, 1, 1]>,
  InstrItinData<IIC_iStore_bh_si,[InstrStage<2, [A8_Pipe0, A8_Pipe1], 0>,
                                  InstrStage<2, [A8_LSPipe]>], [3, 1, 1]>,
  //
  // Immediate offset with update
  InstrItinData<IIC_iStore_iu , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [2, 3, 1]>,
  InstrItinData<IIC_iStore_bh_iu,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<1, [A8_LSPipe]>], [2, 3, 1]>,
  //
  // Register offset with update
  InstrItinData<IIC_iStore_ru  , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                  InstrStage<1, [A8_LSPipe]>], [2, 3, 1, 1]>,
  InstrItinData<IIC_iStore_bh_ru,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                  InstrStage<1, [A8_LSPipe]>], [2, 3, 1, 1]>,
  InstrItinData<IIC_iStore_d_ru, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                  InstrStage<1, [A8_LSPipe]>], [2, 3, 1, 1]>,
  //
  // Scaled register offset with update, issues over 2 cycles
  InstrItinData<IIC_iStore_siu, [InstrStage<2, [A8_Pipe0, A8_Pipe1], 0>,
                                 InstrStage<2, [A8_LSPipe]>], [3, 3, 1, 1]>,
  InstrItinData<IIC_iStore_bh_siu,[InstrStage<2, [A8_Pipe0, A8_Pipe1], 0>,
                                   InstrStage<2, [A8_LSPipe]>], [3, 3, 1, 1]>,
  //
  // Store multiple. Pipeline 0 only.
  // FIXME: A8_LSPipe cycle time is dynamic, this assumes 3 to 4 registers.
  InstrItinData<IIC_iStore_m , [InstrStage<2, [A8_Pipe0], 0>,
                                InstrStage<2, [A8_LSPipe]>],
                [], [], -1>, // dynamic uops
  //
  // Store multiple + update
  InstrItinData<IIC_iStore_mu, [InstrStage<2, [A8_Pipe0], 0>,
                                InstrStage<2, [A8_LSPipe]>],
                [2], [], -1>, // dynamic uops
  //
  // Preload
  InstrItinData<IIC_Preload, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>], [2, 2]>,

  // Branch
  //
  // no delay slots, so the latency of a branch is unimportant
  InstrItinData<IIC_Br      , [InstrStage<1, [A8_Pipe0, A8_Pipe1]>]>,

  // VFP
  // Issue through integer pipeline, and execute in NEON unit. We assume
  // RunFast mode so that NFP pipeline is used for single-precision when
  // possible.
  //
  // FP Special Register to Integer Register File Move
  InstrItinData<IIC_fpSTAT , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                              InstrStage<1, [A8_NLSPipe]>], [20]>,
  //
  // Single-precision FP Unary
  InstrItinData<IIC_fpUNA32 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [7, 1]>,
  //
  // Double-precision FP Unary
  InstrItinData<IIC_fpUNA64 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NPipe], 0>,
                               InstrStage<4, [A8_NLSPipe]>], [4, 1]>,
  //
  // Single-precision FP Compare
  InstrItinData<IIC_fpCMP32 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [1, 1]>,
  //
  // Double-precision FP Compare
  InstrItinData<IIC_fpCMP64 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NPipe], 0>,
                               InstrStage<4, [A8_NLSPipe]>], [4, 1]>,
  //
  // Single to Double FP Convert
  InstrItinData<IIC_fpCVTSD , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<7, [A8_NPipe], 0>,
                               InstrStage<7, [A8_NLSPipe]>], [7, 1]>,
  //
  // Double to Single FP Convert
  InstrItinData<IIC_fpCVTDS , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<5, [A8_NPipe], 0>,
                               InstrStage<5, [A8_NLSPipe]>], [5, 1]>,
  //
  // Single-Precision FP to Integer Convert
  InstrItinData<IIC_fpCVTSI , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [7, 1]>,
  //
  // Double-Precision FP to Integer Convert
  InstrItinData<IIC_fpCVTDI , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<8, [A8_NPipe], 0>,
                               InstrStage<8, [A8_NLSPipe]>], [8, 1]>,
  //
  // Integer to Single-Precision FP Convert
  InstrItinData<IIC_fpCVTIS , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [7, 1]>,
  //
  // Integer to Double-Precision FP Convert
  InstrItinData<IIC_fpCVTID , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<8, [A8_NPipe], 0>,
                               InstrStage<8, [A8_NLSPipe]>], [8, 1]>,
  //
  // Single-precision FP ALU
  InstrItinData<IIC_fpALU32 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [7, 1, 1]>,
  //
  // Double-precision FP ALU
  InstrItinData<IIC_fpALU64 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<9, [A8_NPipe], 0>,
                               InstrStage<9, [A8_NLSPipe]>], [9, 1, 1]>,
  //
  // Single-precision FP Multiply
  InstrItinData<IIC_fpMUL32 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [7, 1, 1]>,
  //
  // Double-precision FP Multiply
  InstrItinData<IIC_fpMUL64 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<11, [A8_NPipe], 0>,
                               InstrStage<11, [A8_NLSPipe]>], [11, 1, 1]>,
  //
  // Single-precision FP MAC
  InstrItinData<IIC_fpMAC32 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [7, 2, 1, 1]>,
  //
  // Double-precision FP MAC
  InstrItinData<IIC_fpMAC64 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<19, [A8_NPipe], 0>,
                               InstrStage<19, [A8_NLSPipe]>], [19, 2, 1, 1]>,
  //
  // Single-precision Fused FP MAC
  InstrItinData<IIC_fpFMAC32, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [7, 2, 1, 1]>,
  //
  // Double-precision Fused FP MAC
  InstrItinData<IIC_fpFMAC64, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<19, [A8_NPipe], 0>,
                               InstrStage<19, [A8_NLSPipe]>], [19, 2, 1, 1]>,
  //
  // Single-precision FP DIV
  InstrItinData<IIC_fpDIV32 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<20, [A8_NPipe], 0>,
                               InstrStage<20, [A8_NLSPipe]>], [20, 1, 1]>,
  //
  // Double-precision FP DIV
  InstrItinData<IIC_fpDIV64 , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<29, [A8_NPipe], 0>,
                               InstrStage<29, [A8_NLSPipe]>], [29, 1, 1]>,
  //
  // Single-precision FP SQRT
  InstrItinData<IIC_fpSQRT32, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<19, [A8_NPipe], 0>,
                               InstrStage<19, [A8_NLSPipe]>], [19, 1]>,
  //
  // Double-precision FP SQRT
  InstrItinData<IIC_fpSQRT64, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<29, [A8_NPipe], 0>,
                               InstrStage<29, [A8_NLSPipe]>], [29, 1]>,

  //
  // Integer to Single-precision Move
  InstrItinData<IIC_fpMOVIS,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>],
                              [2, 1]>,
  //
  // Integer to Double-precision Move
  InstrItinData<IIC_fpMOVID,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>],
                              [2, 1, 1]>,
  //
  // Single-precision to Integer Move
  InstrItinData<IIC_fpMOVSI,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>],
                              [20, 1]>,
  //
  // Double-precision to Integer Move
  InstrItinData<IIC_fpMOVDI,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>],
                              [20, 20, 1]>,

  //
  // Single-precision FP Load
  InstrItinData<IIC_fpLoad32, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>],
                              [2, 1]>,
  //
  // Double-precision FP Load
  InstrItinData<IIC_fpLoad64, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>],
                              [2, 1]>,
  //
  // FP Load Multiple
  // FIXME: A8_LSPipe cycle time is dynamic, this assumes 3 to 4 registers.
  InstrItinData<IIC_fpLoad_m, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>],
                [1, 1, 1, 2], [], -1>, // dynamic uops
  //
  // FP Load Multiple + update
  InstrItinData<IIC_fpLoad_mu,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>],
                [2, 1, 1, 1, 2], [], -1>, // dynamic uops
  //
  // Single-precision FP Store
  InstrItinData<IIC_fpStore32,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>],
                              [1, 1]>,
  //
  // Double-precision FP Store
  InstrItinData<IIC_fpStore64,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>],
                              [1, 1]>,
  //
  // FP Store Multiple
  InstrItinData<IIC_fpStore_m,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>,
                               InstrStage<1, [A8_NLSPipe], 0>,
                               InstrStage<1, [A8_LSPipe]>],
                [1, 1, 1, 1], [], -1>, // dynamic uops
  //
  // FP Store Multiple + update
  InstrItinData<IIC_fpStore_mu,[InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                                InstrStage<1, [A8_NLSPipe], 0>,
                                InstrStage<1, [A8_LSPipe]>,
                                InstrStage<1, [A8_NLSPipe], 0>,
                                InstrStage<1, [A8_LSPipe]>],
                [2, 1, 1, 1, 1], [], -1>, // dynamic uops
  // NEON
  // Issue through integer pipeline, and execute in NEON unit.
  //
  // VLD1
  InstrItinData<IIC_VLD1,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 1]>,
  // VLD1x2
  InstrItinData<IIC_VLD1x2,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 2, 1]>,
  //
  // VLD1x3
  InstrItinData<IIC_VLD1x3,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 1]>,
  //
  // VLD1x4
  InstrItinData<IIC_VLD1x4,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 3, 1]>,
  //
  // VLD1u
  InstrItinData<IIC_VLD1u,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 2, 1]>,
  //
  // VLD1x2u
  InstrItinData<IIC_VLD1x2u,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 2, 2, 1]>,
  //
  // VLD1x3u
  InstrItinData<IIC_VLD1x3u,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 2, 1]>,
  //
  // VLD1x4u
  InstrItinData<IIC_VLD1x4u,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 3, 2, 1]>,
  //
  // VLD1ln
  InstrItinData<IIC_VLD1ln,   [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [3, 1, 1, 1]>,
  //
  // VLD1lnu
  InstrItinData<IIC_VLD1lnu,  [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [3, 2, 1, 1, 1, 1]>,
  //
  // VLD1dup
  InstrItinData<IIC_VLD1dup,  [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 1]>,
  //
  // VLD1dupu
  InstrItinData<IIC_VLD1dupu, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 2, 1, 1]>,
  //
  // VLD2
  InstrItinData<IIC_VLD2,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 2, 1]>,
  //
  // VLD2x2
  InstrItinData<IIC_VLD2x2,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 3, 1]>,
  //
  // VLD2ln
  InstrItinData<IIC_VLD2ln,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [3, 3, 1, 1, 1, 1]>,
  //
  // VLD2u
  InstrItinData<IIC_VLD2u,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 2, 2, 1, 1, 1]>,
  //
  // VLD2x2u
  InstrItinData<IIC_VLD2x2u,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 3, 2, 1]>,
  //
  // VLD2lnu
  InstrItinData<IIC_VLD2lnu,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [3, 3, 2, 1, 1, 1, 1, 1]>,
  //
  // VLD2dup
  InstrItinData<IIC_VLD2dup,  [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 2, 1]>,
  //
  // VLD2dupu
  InstrItinData<IIC_VLD2dupu, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 2, 2, 1, 1]>,
  //
  // VLD3
  InstrItinData<IIC_VLD3,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [3, 3, 4, 1]>,
  //
  // VLD3ln
  InstrItinData<IIC_VLD3ln,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<5, [A8_NLSPipe], 0>,
                               InstrStage<5, [A8_LSPipe]>],
                              [4, 4, 5, 1, 1, 1, 1, 2]>,
  //
  // VLD3u
  InstrItinData<IIC_VLD3u,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [3, 3, 4, 2, 1]>,
  //
  // VLD3lnu
  InstrItinData<IIC_VLD3lnu,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<5, [A8_NLSPipe], 0>,
                               InstrStage<5, [A8_LSPipe]>],
                              [4, 4, 5, 2, 1, 1, 1, 1, 1, 2]>,
  //
  // VLD3dup
  InstrItinData<IIC_VLD3dup,  [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 1]>,
  //
  // VLD3dupu
  InstrItinData<IIC_VLD3dupu, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 2, 1, 1]>,
  //
  // VLD4
  InstrItinData<IIC_VLD4,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [3, 3, 4, 4, 1]>,
  //
  // VLD4ln
  InstrItinData<IIC_VLD4ln,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<5, [A8_NLSPipe], 0>,
                               InstrStage<5, [A8_LSPipe]>],
                              [4, 4, 5, 5, 1, 1, 1, 1, 2, 2]>,
  //
  // VLD4u
  InstrItinData<IIC_VLD4u,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [3, 3, 4, 4, 2, 1]>,
  //
  // VLD4lnu
  InstrItinData<IIC_VLD4lnu,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<5, [A8_NLSPipe], 0>,
                               InstrStage<5, [A8_LSPipe]>],
                              [4, 4, 5, 5, 2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // VLD4dup
  InstrItinData<IIC_VLD4dup,  [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 3, 1]>,
  //
  // VLD4dupu
  InstrItinData<IIC_VLD4dupu, [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 2, 3, 3, 2, 1, 1]>,
  //
  // VST1
  InstrItinData<IIC_VST1,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [1, 1, 1]>,
  //
  // VST1x2
  InstrItinData<IIC_VST1x2,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [1, 1, 1, 1]>,
  //
  // VST1x3
  InstrItinData<IIC_VST1x3,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [1, 1, 1, 1, 2]>,
  //
  // VST1x4
  InstrItinData<IIC_VST1x4,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [1, 1, 1, 1, 2, 2]>,
  //
  // VST1u
  InstrItinData<IIC_VST1u,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1]>,
  //
  // VST1x2u
  InstrItinData<IIC_VST1x2u,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1]>,
  //
  // VST1x3u
  InstrItinData<IIC_VST1x3u,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1, 2]>,
  //
  // VST1x4u
  InstrItinData<IIC_VST1x4u,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // VST1ln
  InstrItinData<IIC_VST1ln,   [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [1, 1, 1]>,
  //
  // VST1lnu
  InstrItinData<IIC_VST1lnu,  [InstrStage<1, [A8_Pipe0, A8_Pipe1]>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1]>,
  //
  // VST2
  InstrItinData<IIC_VST2,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [1, 1, 1, 1]>,
  //
  // VST2x2
  InstrItinData<IIC_VST2x2,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [1, 1, 1, 1, 2, 2]>,
  //
  // VST2u
  InstrItinData<IIC_VST2u,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1]>,
  //
  // VST2x2u
  InstrItinData<IIC_VST2x2u,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // VST2ln
  InstrItinData<IIC_VST2ln,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [1, 1, 1, 1]>,
  //
  // VST2lnu
  InstrItinData<IIC_VST2lnu,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<2, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1]>,
  //
  // VST3
  InstrItinData<IIC_VST3,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [1, 1, 1, 1, 2]>,
  //
  // VST3u
  InstrItinData<IIC_VST3u,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1, 2]>,
  //
  // VST3ln
  InstrItinData<IIC_VST3ln,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [1, 1, 1, 1, 2]>,
  //
  // VST3lnu
  InstrItinData<IIC_VST3lnu,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<3, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1, 2]>,
  //
  // VST4
  InstrItinData<IIC_VST4,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [1, 1, 1, 1, 2, 2]>,
  //
  // VST4u
  InstrItinData<IIC_VST4u,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // VST4ln
  InstrItinData<IIC_VST4ln,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [1, 1, 1, 1, 2, 2]>,
  //
  // VST4lnu
  InstrItinData<IIC_VST4lnu,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<4, [A8_NLSPipe], 0>,
                               InstrStage<4, [A8_LSPipe]>],
                              [2, 1, 1, 1, 1, 1, 2, 2]>,
  //
  // Double-register FP Unary
  InstrItinData<IIC_VUNAD,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [5, 2]>,
  //
  // Quad-register FP Unary
  // Result written in N5, but that is relative to the last cycle of multicycle,
  // so we use 6 for those cases
  InstrItinData<IIC_VUNAQ,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [6, 2]>,
  //
  // Double-register FP Binary
  InstrItinData<IIC_VBIND,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [5, 2, 2]>,
  //
  // VPADD, etc.
  InstrItinData<IIC_VPBIND,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [5, 2, 2]>,
  //
  // Double-register FP VMUL
  InstrItinData<IIC_VFMULD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [5, 2, 1]>,

  //
  // Quad-register FP Binary
  // Result written in N5, but that is relative to the last cycle of multicycle,
  // so we use 6 for those cases
  InstrItinData<IIC_VBINQ,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [6, 2, 2]>,
  //
  // Quad-register FP VMUL
  InstrItinData<IIC_VFMULQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [6, 2, 1]>,
  //
  // Move
  InstrItinData<IIC_VMOV,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [1, 1]>,
  //
  // Move Immediate
  InstrItinData<IIC_VMOVImm,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [3]>,
  //
  // Double-register Permute Move
  InstrItinData<IIC_VMOVD,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>], [2, 1]>,
  //
  // Quad-register Permute Move
  // Result written in N2, but that is relative to the last cycle of multicycle,
  // so we use 3 for those cases
  InstrItinData<IIC_VMOVQ,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [3, 1]>,
  //
  // Integer to Single-precision Move
  InstrItinData<IIC_VMOVIS ,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>], [2, 1]>,
  //
  // Integer to Double-precision Move
  InstrItinData<IIC_VMOVID ,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>], [2, 1, 1]>,
  //
  // Single-precision to Integer Move
  InstrItinData<IIC_VMOVSI ,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>], [20, 1]>,
  //
  // Double-precision to Integer Move
  InstrItinData<IIC_VMOVDI ,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>], [20, 20, 1]>,
  //
  // Integer to Lane Move
  InstrItinData<IIC_VMOVISL , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [3, 1, 1]>,
  //
  // Vector narrow move
  InstrItinData<IIC_VMOVN   , [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [2, 1]>,
  //
  // Double-register Permute
  InstrItinData<IIC_VPERMD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>], [2, 2, 1, 1]>,
  //
  // Quad-register Permute
  // Result written in N2, but that is relative to the last cycle of multicycle,
  // so we use 3 for those cases
  InstrItinData<IIC_VPERMQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [3, 3, 1, 1]>,
  //
  // Quad-register Permute (3 cycle issue)
  // Result written in N2, but that is relative to the last cycle of multicycle,
  // so we use 4 for those cases
  InstrItinData<IIC_VPERMQ3,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>,
                               InstrStage<1, [A8_NPipe], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [4, 4, 1, 1]>,
  //
  // Double-register FP Multiple-Accumulate
  InstrItinData<IIC_VMACD,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [9, 3, 2, 2]>,
  //
  // Quad-register FP Multiple-Accumulate
  // Result written in N9, but that is relative to the last cycle of multicycle,
  // so we use 10 for those cases
  InstrItinData<IIC_VMACQ,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [10, 3, 2, 2]>,
  //
  // Double-register Fused FP Multiple-Accumulate
  InstrItinData<IIC_VFMACD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [9, 3, 2, 2]>,
  //
  // Quad-register Fused FP Multiple-Accumulate
  // Result written in N9, but that is relative to the last cycle of multicycle,
  // so we use 10 for those cases
  InstrItinData<IIC_VFMACQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [10, 3, 2, 2]>,
  //
  // Double-register Reciprical Step
  InstrItinData<IIC_VRECSD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [9, 2, 2]>,
  //
  // Quad-register Reciprical Step
  InstrItinData<IIC_VRECSQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [10, 2, 2]>,
  //
  // Double-register Integer Count
  InstrItinData<IIC_VCNTiD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [3, 2, 2]>,
  //
  // Quad-register Integer Count
  // Result written in N3, but that is relative to the last cycle of multicycle,
  // so we use 4 for those cases
  InstrItinData<IIC_VCNTiQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [4, 2, 2]>,
  //
  // Double-register Integer Unary
  InstrItinData<IIC_VUNAiD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 2]>,
  //
  // Quad-register Integer Unary
  InstrItinData<IIC_VUNAiQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 2]>,
  //
  // Double-register Integer Q-Unary
  InstrItinData<IIC_VQUNAiD,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 1]>,
  //
  // Quad-register Integer CountQ-Unary
  InstrItinData<IIC_VQUNAiQ,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 1]>,
  //
  // Double-register Integer Binary
  InstrItinData<IIC_VBINiD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [3, 2, 2]>,
  //
  // Quad-register Integer Binary
  InstrItinData<IIC_VBINiQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [3, 2, 2]>,
  //
  // Double-register Integer Binary (4 cycle)
  InstrItinData<IIC_VBINi4D,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 2, 1]>,
  //
  // Quad-register Integer Binary (4 cycle)
  InstrItinData<IIC_VBINi4Q,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 2, 1]>,

  //
  // Double-register Integer Subtract
  InstrItinData<IIC_VSUBiD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [3, 2, 1]>,
  //
  // Quad-register Integer Subtract
  InstrItinData<IIC_VSUBiQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [3, 2, 1]>,
  //
  // Double-register Integer Subtract
  InstrItinData<IIC_VSUBi4D,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 2, 1]>,
  //
  // Quad-register Integer Subtract
  InstrItinData<IIC_VSUBi4Q,  [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 2, 1]>,
  //
  // Double-register Integer Shift
  InstrItinData<IIC_VSHLiD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [3, 1, 1]>,
  //
  // Quad-register Integer Shift
  InstrItinData<IIC_VSHLiQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [4, 1, 1]>,
  //
  // Double-register Integer Shift (4 cycle)
  InstrItinData<IIC_VSHLi4D,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [4, 1, 1]>,
  //
  // Quad-register Integer Shift (4 cycle)
  InstrItinData<IIC_VSHLi4Q,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [5, 1, 1]>,
  //
  // Double-register Integer Pair Add Long
  InstrItinData<IIC_VPALiD,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [6, 3, 1]>,
  //
  // Quad-register Integer Pair Add Long
  InstrItinData<IIC_VPALiQ,   [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [7, 3, 1]>,
  //
  // Double-register Absolute Difference and Accumulate
  InstrItinData<IIC_VABAD,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [6, 3, 2, 1]>,
  //
  // Quad-register Absolute Difference and Accumulate
  InstrItinData<IIC_VABAQ,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [6, 3, 2, 1]>,

  //
  // Double-register Integer Multiply (.8, .16)
  InstrItinData<IIC_VMULi16D, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [6, 2, 2]>,
  //
  // Double-register Integer Multiply (.32)
  InstrItinData<IIC_VMULi32D, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [7, 2, 1]>,
  //
  // Quad-register Integer Multiply (.8, .16)
  InstrItinData<IIC_VMULi16Q, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [7, 2, 2]>,
  //
  // Quad-register Integer Multiply (.32)
  InstrItinData<IIC_VMULi32Q, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_NPipe]>], [9, 2, 1]>,
  //
  // Double-register Integer Multiply-Accumulate (.8, .16)
  InstrItinData<IIC_VMACi16D, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>], [6, 3, 2, 2]>,
  //
  // Double-register Integer Multiply-Accumulate (.32)
  InstrItinData<IIC_VMACi32D, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [7, 3, 2, 1]>,
  //
  // Quad-register Integer Multiply-Accumulate (.8, .16)
  InstrItinData<IIC_VMACi16Q, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NPipe]>], [7, 3, 2, 2]>,
  //
  // Quad-register Integer Multiply-Accumulate (.32)
  InstrItinData<IIC_VMACi32Q, [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NPipe]>,
                               InstrStage<2, [A8_NLSPipe], 0>,
                               InstrStage<3, [A8_NPipe]>], [9, 3, 2, 1]>,
  //
  // Double-register VEXT
  InstrItinData<IIC_VEXTD,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>], [2, 1, 1]>,
  //
  // Quad-register VEXT
  InstrItinData<IIC_VEXTQ,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [3, 1, 1]>,
  //
  // VTB
  InstrItinData<IIC_VTB1,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [3, 2, 1]>,
  InstrItinData<IIC_VTB2,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [3, 2, 2, 1]>,
  InstrItinData<IIC_VTB3,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>,
                               InstrStage<1, [A8_NPipe], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [4, 2, 2, 3, 1]>,
  InstrItinData<IIC_VTB4,     [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>,
                               InstrStage<1, [A8_NPipe], 0>,
                               InstrStage<2, [A8_NLSPipe]>],[4, 2, 2, 3, 3, 1]>,
  //
  // VTBX
  InstrItinData<IIC_VTBX1,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [3, 1, 2, 1]>,
  InstrItinData<IIC_VTBX2,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<2, [A8_NLSPipe]>], [3, 1, 2, 2, 1]>,
  InstrItinData<IIC_VTBX3,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>,
                               InstrStage<1, [A8_NPipe], 0>,
                               InstrStage<2, [A8_NLSPipe]>],[4, 1, 2, 2, 3, 1]>,
  InstrItinData<IIC_VTBX4,    [InstrStage<1, [A8_Pipe0, A8_Pipe1], 0>,
                               InstrStage<1, [A8_NLSPipe]>,
                               InstrStage<1, [A8_NPipe], 0>,
                            InstrStage<2, [A8_NLSPipe]>], [4, 1, 2, 2, 3, 3, 1]>
]>;

// ===---------------------------------------------------------------------===//
// This following definitions describe the simple machine model which
// will replace itineraries.

// Cortex-A8 machine model for scheduling and other instruction cost heuristics.
def CortexA8Model : SchedMachineModel {
  let IssueWidth = 2; // 2 micro-ops are dispatched per cycle.
  let LoadLatency = 2; // Optimistic load latency assuming bypass.
                       // This is overriden by OperandCycles if the
                       // Itineraries are queried instead.
  let MispredictPenalty = 13; // Based on estimate of pipeline depth.
  let CompleteModel = 0;

  let Itineraries = CortexA8Itineraries;
}
