//=- AArch64SchedExynosM4.td - Samsung Exynos M4 Sched Defs --*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the Samsung Exynos M4 to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// The Exynos-M4 is an advanced superscalar microprocessor with a 6-wide
// in-order stage for decode and dispatch and a wider issue stage.
// The execution units and loads and stores are out-of-order.

def ExynosM4Model : SchedMachineModel {
  let IssueWidth            =   6; // Up to 6 uops per cycle.
  let MicroOpBufferSize     = 228; // ROB size.
  let LoopMicroOpBufferSize =  48; // Based on the instruction queue size.
  let LoadLatency           =   4; // Optimistic load cases.
  let MispredictPenalty     =  16; // Minimum branch misprediction penalty.
  let CompleteModel         =   1; // Use the default model otherwise.

  list<Predicate> UnsupportedFeatures = [HasSVE];
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on the Exynos-M4.

let SchedModel = ExynosM4Model in {

def M4UnitA  : ProcResource<2>; // Simple integer
def M4UnitC  : ProcResource<2>; // Simple and complex integer
let Super =  M4UnitC, BufferSize = 1 in
def M4UnitD  : ProcResource<1>; // Integer division (inside C0, serialized)
let Super =  M4UnitC in
def M4UnitE  : ProcResource<1>; // CRC (inside C0)
def M4UnitB  : ProcResource<2>; // Branch
def M4UnitL0 : ProcResource<1>; // Load
def M4UnitS0 : ProcResource<1>; // Store
def M4PipeLS : ProcResource<1>; // Load/Store
let Super = M4PipeLS in {
  def M4UnitL1 : ProcResource<1>;
  def M4UnitS1 : ProcResource<1>;
}
def M4PipeF0 : ProcResource<1>; // FP #0
let Super = M4PipeF0 in {
  def M4UnitFMAC0 : ProcResource<1>; // FP multiplication
  def M4UnitFADD0 : ProcResource<1>; // Simple FP
  def M4UnitFCVT0 : ProcResource<1>; // FP conversion
  def M4UnitNALU0 : ProcResource<1>; // Simple vector
  def M4UnitNHAD  : ProcResource<1>; // Horizontal vector
  def M4UnitNMSC  : ProcResource<1>; // FP and vector miscellanea
  def M4UnitNMUL0 : ProcResource<1>; // Vector multiplication
  def M4UnitNSHT0 : ProcResource<1>; // Vector shifting
  def M4UnitNSHF0 : ProcResource<1>; // Vector shuffling
  def M4UnitNCRY0 : ProcResource<1>; // Cryptographic
}
def M4PipeF1 : ProcResource<1>; // FP #1
let Super = M4PipeF1 in {
  def M4UnitFMAC1 : ProcResource<1>; // FP multiplication
  def M4UnitFADD1 : ProcResource<1>; // Simple FP
  def M4UnitFDIV0 : ProcResource<2>; // FP division (serialized)
  def M4UnitFSQR0 : ProcResource<2>; // FP square root (serialized)
  def M4UnitFST0  : ProcResource<1>; // FP store
  def M4UnitNALU1 : ProcResource<1>; // Simple vector
  def M4UnitNSHT1 : ProcResource<1>; // Vector shifting
  def M4UnitNSHF1 : ProcResource<1>; // Vector shuffling
}
def M4PipeF2 : ProcResource<1>; // FP #2
let Super = M4PipeF2 in {
  def M4UnitFMAC2 : ProcResource<1>; // FP multiplication
  def M4UnitFADD2 : ProcResource<1>; // Simple FP
  def M4UnitFCVT1 : ProcResource<1>; // FP conversion
  def M4UnitFDIV1 : ProcResource<2>; // FP division (serialized)
  def M4UnitFSQR1 : ProcResource<2>; // FP square root (serialized)
  def M4UnitFST1  : ProcResource<1>; // FP store
  def M4UnitNALU2 : ProcResource<1>; // Simple vector
  def M4UnitNMUL1 : ProcResource<1>; // Vector multiplication
  def M4UnitNSHT2 : ProcResource<1>; // Vector shifting
  def M4UnitNCRY1 : ProcResource<1>; // Cryptographic
}

def M4UnitALU   : ProcResGroup<[M4UnitA,
                                M4UnitC]>;
def M4UnitL     : ProcResGroup<[M4UnitL0,
                                M4UnitL1]>;
def M4UnitS     : ProcResGroup<[M4UnitS0,
                                M4UnitS1]>;
def M4UnitFMAC  : ProcResGroup<[M4UnitFMAC0,
                                M4UnitFMAC1,
                                M4UnitFMAC2]>;
def M4UnitFMACH : ProcResGroup<[M4UnitFMAC0,
                                M4UnitFMAC1]>;
def M4UnitFADD  : ProcResGroup<[M4UnitFADD0,
                                M4UnitFADD1,
                                M4UnitFADD2]>;
def M4UnitFADDH : ProcResGroup<[M4UnitFADD0,
                                M4UnitFADD1]>;
def M4UnitFCVT  : ProcResGroup<[M4UnitFCVT0,
                                M4UnitFCVT1]>;
def M4UnitFCVTH : ProcResGroup<[M4UnitFCVT0]>;
def M4UnitFDIV  : ProcResGroup<[M4UnitFDIV0,
                                M4UnitFDIV1]>;
def M4UnitFDIVH : ProcResGroup<[M4UnitFDIV0]>;
def M4UnitFSQR  : ProcResGroup<[M4UnitFSQR0,
                                M4UnitFSQR1]>;
def M4UnitFSQRH : ProcResGroup<[M4UnitFSQR0]>;
def M4UnitFST   : ProcResGroup<[M4UnitFST0,
                                M4UnitFST1]>;
def M4UnitNALU  : ProcResGroup<[M4UnitNALU0,
                                M4UnitNALU1,
                                M4UnitNALU2]>;
def M4UnitNALUH : ProcResGroup<[M4UnitNALU0,
                                M4UnitNALU1]>;
def M4UnitNMUL  : ProcResGroup<[M4UnitNMUL0,
                                M4UnitNMUL1]>;
def M4UnitNSHT  : ProcResGroup<[M4UnitNSHT0,
                                M4UnitNSHT1,
                                M4UnitNSHT2]>;
def M4UnitNSHF  : ProcResGroup<[M4UnitNSHF0,
                                M4UnitNSHF1]>;
def M4UnitNSHFH : ProcResGroup<[M4UnitNSHF0]>;
def M4UnitNCRY  : ProcResGroup<[M4UnitNCRY0,
                                M4UnitNCRY1]>;

//===----------------------------------------------------------------------===//
// Resources details.

def M4WriteZ0 : SchedWriteRes<[]> { let Latency = 0; }
def M4WriteZ1 : SchedWriteRes<[]> { let Latency = 1;
                                    let NumMicroOps = 0; }
def M4WriteZ4 : SchedWriteRes<[]> { let Latency = 4;
                                    let NumMicroOps = 0; }

def M4WriteA1 : SchedWriteRes<[M4UnitALU]> { let Latency = 1; }
def M4WriteA2 : SchedWriteRes<[M4UnitALU]> { let Latency = 2; }
def M4WriteAA : SchedWriteRes<[M4UnitALU]> { let Latency = 2;
                                             let ResourceCycles = [2]; }
def M4WriteAB : SchedWriteRes<[M4UnitALU,
                               M4UnitC]>   { let Latency = 2;
                                             let NumMicroOps = 2; }
def M4WriteAC : SchedWriteRes<[M4UnitALU,
                               M4UnitALU,
                               M4UnitC]>   { let Latency = 3;
                                             let NumMicroOps = 3; }
def M4WriteAD : SchedWriteRes<[M4UnitALU,
                               M4UnitC]>   { let Latency = 2;
                                             let NumMicroOps = 2; }
def M4WriteAF : SchedWriteRes<[M4UnitALU]> { let Latency = 2;
                                             let NumMicroOps = 2; }
def M4WriteAU : SchedWriteVariant<[SchedVar<IsCopyIdiomPred,   [M4WriteZ0]>,
                                   SchedVar<ExynosArithPred,   [M4WriteA1]>,
                                   SchedVar<ExynosLogicExPred, [M4WriteA1]>,
                                   SchedVar<NoSchedPred,       [M4WriteAA]>]>;
def M4WriteAV : SchedWriteVariant<[SchedVar<ExynosResetPred, [M4WriteZ0]>,
                                   SchedVar<NoSchedPred,     [M4WriteAA]>]>;
def M4WriteAX : SchedWriteVariant<[SchedVar<ExynosArithPred,   [M4WriteA1]>,
                                   SchedVar<ExynosLogicExPred, [M4WriteA1]>,
                                   SchedVar<NoSchedPred,       [M4WriteAA]>]>;
def M4WriteAY : SchedWriteVariant<[SchedVar<ExynosRotateRightImmPred, [M4WriteA1]>,
                                   SchedVar<NoSchedPred,              [M4WriteAF]>]>;

def M4WriteB1 : SchedWriteRes<[M4UnitB]> { let Latency = 1; }
def M4WriteBX : SchedWriteVariant<[SchedVar<ExynosBranchLinkLRPred, [M4WriteAC]>,
                                   SchedVar<NoSchedPred,            [M4WriteAB]>]>;

def M4WriteC1 : SchedWriteRes<[M4UnitC]> { let Latency = 1; }
def M4WriteC3 : SchedWriteRes<[M4UnitC]> { let Latency = 3; }
def M4WriteCA : SchedWriteRes<[M4UnitC]> { let Latency = 4;
                                           let ResourceCycles = [2]; }

def M4WriteD12 : SchedWriteRes<[M4UnitD]> { let Latency = 12; }
def M4WriteD21 : SchedWriteRes<[M4UnitD]> { let Latency = 21; }

def M4WriteE2 : SchedWriteRes<[M4UnitE]> { let Latency = 2; }

def M4WriteL4 : SchedWriteRes<[M4UnitL]> { let Latency = 4; }
def M4WriteL5 : SchedWriteRes<[M4UnitL]> { let Latency = 5; }
def M4WriteLA : SchedWriteRes<[M4UnitL,
                               M4UnitL]> { let Latency = 5;
                                           let NumMicroOps = 1; }
def M4WriteLB : SchedWriteRes<[M4UnitA,
                               M4UnitL]> { let Latency = 5;
                                           let NumMicroOps = 2; }
def M4WriteLC : SchedWriteRes<[M4UnitA,
                               M4UnitL,
                               M4UnitL]> { let Latency = 5;
                                           let NumMicroOps = 2; }
def M4WriteLD : SchedWriteRes<[M4UnitA,
                               M4UnitL]> { let Latency = 4;
                                           let NumMicroOps = 2; }
def M4WriteLE : SchedWriteRes<[M4UnitA,
                               M4UnitL]> { let Latency = 6;
                                           let NumMicroOps = 2; }
def M4WriteLH : SchedWriteRes<[]>        { let Latency = 5;
                                           let NumMicroOps = 0; }
def M4WriteLX : SchedWriteVariant<[SchedVar<ScaledIdxPred, [M4WriteL5]>,
                                   SchedVar<NoSchedPred,   [M4WriteL4]>]>;

def M4WriteS1 : SchedWriteRes<[M4UnitS]>  { let Latency = 1; }
def M4WriteSA : SchedWriteRes<[M4UnitS0]> { let Latency = 3; }
def M4WriteSB : SchedWriteRes<[M4UnitA,
                               M4UnitS]>  { let Latency = 2;
                                            let NumMicroOps = 1; }
def M4WriteSX : SchedWriteVariant<[SchedVar<ExynosScaledIdxPred, [M4WriteSB]>,
                                   SchedVar<NoSchedPred,         [M4WriteS1]>]>;

def M4ReadAdrBase : SchedReadVariant<[SchedVar<
                                        MCSchedPredicate<
                                          CheckAny<
                                            [ScaledIdxFn,
                                             ExynosScaledIdxFn]>>, [ReadDefault]>,
                                      SchedVar<NoSchedPred,        [ReadDefault]>]>;

def M4WriteNEONA   : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitFADD]>  { let Latency = 3;
                                                    let NumMicroOps = 2; }
def M4WriteNEONB   : SchedWriteRes<[M4UnitNALU,
                                    M4UnitS0]>    { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M4WriteNEOND   : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitFST]>   { let Latency = 6;
                                                    let NumMicroOps = 2; }
def M4WriteNEONH   : SchedWriteRes<[M4UnitNALU,
                                    M4UnitS0]>    { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M4WriteNEONI   : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitS0]>    { let Latency = 2;
                                                    let NumMicroOps = 2; }
def M4WriteNEONJ   : SchedWriteRes<[M4UnitNMSC,
                                    M4UnitS0]>    { let Latency = 4; }
def M4WriteNEONK   : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitNMSC,
                                    M4UnitS0]>    { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M4WriteNEONL   : SchedWriteRes<[M4UnitNMUL]>  { let Latency = 3; }
def M4WriteNEONM   : SchedWriteRes<[M4UnitNMUL]>  { let Latency = 3; }
def M4WriteNEONN   : SchedWriteRes<[M4UnitNMSC,
                                    M4UnitNMSC]>  { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M4WriteNEONO   : SchedWriteRes<[M4UnitNMSC,
                                    M4UnitNMSC,
                                    M4UnitNMSC]>  { let Latency = 8;
                                                    let NumMicroOps = 3; }
def M4WriteNEONP   : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitNMSC]>  { let Latency = 4;
                                                    let NumMicroOps = 2; }
def M4WriteNEONQ   : SchedWriteRes<[M4UnitNMSC,
                                    M4UnitC]>     { let Latency = 3;
                                                    let NumMicroOps = 1; }
def M4WriteNEONR   : SchedWriteRes<[M4UnitFCVT0,
                                    M4UnitS0]>    { let Latency = 4;
                                                    let NumMicroOps = 1; }
def M4WriteNEONV   : SchedWriteRes<[M4UnitFDIV,
                                    M4UnitFDIV]>  { let Latency = 7;
                                                    let ResourceCycles = [6, 6]; }
def M4WriteNEONVH  : SchedWriteRes<[M4UnitFDIVH,
                                    M4UnitFDIVH]> { let Latency = 7;
                                                    let ResourceCycles = [6, 6]; }
def M4WriteNEONW   : SchedWriteRes<[M4UnitFDIV,
                                    M4UnitFDIV]>  { let Latency = 12;
                                                    let ResourceCycles = [9, 9]; }
def M4WriteNEONX   : SchedWriteRes<[M4UnitFSQR,
                                    M4UnitFSQR]>  { let Latency = 8;
                                                    let ResourceCycles = [7, 7]; }
def M4WriteNEONXH  : SchedWriteRes<[M4UnitFSQRH,
                                    M4UnitFSQRH]> { let Latency = 7;
                                                    let ResourceCycles = [6, 6]; }
def M4WriteNEONY   : SchedWriteRes<[M4UnitFSQR,
                                    M4UnitFSQR]>  { let Latency = 12;
                                                    let ResourceCycles = [9, 9]; }
def M4WriteNEONZ   : SchedWriteVariant<[SchedVar<ExynosQFormPred, [M4WriteNEONO]>,
                                        SchedVar<NoSchedPred,     [M4WriteNEONN]>]>;

def M4WriteFADD2   : SchedWriteRes<[M4UnitFADD]>  { let Latency = 2; }
def M4WriteFADD2H  : SchedWriteRes<[M4UnitFADDH]> { let Latency = 2; }

def M4WriteFCVT2   : SchedWriteRes<[M4UnitFCVT]>  { let Latency = 2; }
def M4WriteFCVT2A  : SchedWriteRes<[M4UnitFCVT0]> { let Latency = 2; }
def M4WriteFCVT2H  : SchedWriteRes<[M4UnitFCVTH]> { let Latency = 2; }
def M4WriteFCVT3   : SchedWriteRes<[M4UnitFCVT]>  { let Latency = 3; }
def M4WriteFCVT3A  : SchedWriteRes<[M4UnitFCVT0]> { let Latency = 3; }
def M4WriteFCVT3H  : SchedWriteRes<[M4UnitFCVTH]> { let Latency = 3; }
def M4WriteFCVT4   : SchedWriteRes<[M4UnitFCVT]>  { let Latency = 4; }
def M4WriteFCVT4A  : SchedWriteRes<[M4UnitFCVT0]> { let Latency = 4; }
def M4WriteFCVT6A  : SchedWriteRes<[M4UnitFCVT0]> { let Latency = 6; }

def M4WriteFDIV7   : SchedWriteRes<[M4UnitFDIV]>  { let Latency = 7;
                                                    let ResourceCycles = [6]; }
def M4WriteFDIV7H  : SchedWriteRes<[M4UnitFDIVH]> { let Latency = 7;
                                                    let ResourceCycles = [6]; }
def M4WriteFDIV12  : SchedWriteRes<[M4UnitFDIV]>  { let Latency = 12;
                                                    let ResourceCycles = [9]; }

def M4WriteFMAC2H  : SchedWriteRes<[M4UnitFMACH]> { let Latency = 2; }
def M4WriteFMAC3H  : SchedWriteRes<[M4UnitFMACH]> { let Latency = 3; }
def M4WriteFMAC3   : SchedWriteRes<[M4UnitFMAC]>  { let Latency = 3; }
def M4WriteFMAC4   : SchedWriteRes<[M4UnitFMAC]>  { let Latency = 4; }
def M4WriteFMAC4H  : SchedWriteRes<[M4UnitFMACH]> { let Latency = 4; }
def M4WriteFMAC5   : SchedWriteRes<[M4UnitFMAC]>  { let Latency = 5; }

def M4WriteFSQR7H  : SchedWriteRes<[M4UnitFSQRH]> { let Latency = 7;
                                                    let ResourceCycles = [6]; }
def M4WriteFSQR8   : SchedWriteRes<[M4UnitFSQR]>  { let Latency = 8;
                                                    let ResourceCycles = [7]; }
def M4WriteFSQR12  : SchedWriteRes<[M4UnitFSQR]>  { let Latency = 12;
                                                    let ResourceCycles = [9]; }

def M4WriteNALU1   : SchedWriteRes<[M4UnitNALU]>  { let Latency = 1; }
def M4WriteNALU1H  : SchedWriteRes<[M4UnitNALUH]> { let Latency = 1; }

def M4WriteNCRY1   : SchedWriteRes<[M4UnitNCRY]>  { let Latency = 1; }
def M4WriteNCRY1A  : SchedWriteRes<[M4UnitNCRY0]> { let Latency = 1; }
def M4WriteNCRY3A  : SchedWriteRes<[M4UnitNCRY0]> { let Latency = 3; }
def M4WriteNCRY5A  : SchedWriteRes<[M4UnitNCRY]>  { let Latency = 5; }

def M4WriteNHAD1   : SchedWriteRes<[M4UnitNHAD]>  { let Latency = 1; }
def M4WriteNHAD3   : SchedWriteRes<[M4UnitNHAD]>  { let Latency = 3; }

def M4WriteNMSC1   : SchedWriteRes<[M4UnitNMSC]>  { let Latency = 1; }
def M4WriteNMSC2   : SchedWriteRes<[M4UnitNMSC]>  { let Latency = 2; }
def M4WriteNMSC3   : SchedWriteRes<[M4UnitNMSC]>  { let Latency = 3; }

def M4WriteNMUL3   : SchedWriteRes<[M4UnitNMUL]>  { let Latency = 3; }

def M4WriteNSHF1   : SchedWriteRes<[M4UnitNSHF]>  { let Latency = 1; }
def M4WriteNSHF1H  : SchedWriteRes<[M4UnitNSHFH]> { let Latency = 1; }
def M4WriteNSHF3   : SchedWriteRes<[M4UnitNSHF]>  { let Latency = 3; }
def M4WriteNSHFA   : SchedWriteRes<[M4UnitNSHF]>  { let Latency = 1;
                                                    let ResourceCycles = [2]; }
def M4WriteNSHFB   : SchedWriteRes<[M4UnitNSHF]>  { let Latency = 2;
                                                    let NumMicroOps = 2;
                                                    let ResourceCycles = [2]; }
def M4WriteNSHFC   : SchedWriteRes<[M4UnitNSHF]>  { let Latency = 3;
                                                    let NumMicroOps = 3;
                                                    let ResourceCycles = [4]; }
def M4WriteNSHFD   : SchedWriteRes<[M4UnitNSHF]>  { let Latency = 4;
                                                    let NumMicroOps = 4;
                                                    let ResourceCycles = [4]; }

def M4WriteNSHT1   : SchedWriteRes<[M4UnitNSHT]>  { let Latency = 1; }
def M4WriteNSHT2   : SchedWriteRes<[M4UnitNSHT]>  { let Latency = 2; }
def M4WriteNSHT3   : SchedWriteRes<[M4UnitNSHT]>  { let Latency = 3; }
def M4WriteNSHT4A  : SchedWriteRes<[M4UnitNSHT1]> { let Latency = 4; }

def M4WriteVLDA    : SchedWriteRes<[M4UnitL,
                                    M4UnitL]>     { let Latency = 5;
                                                    let NumMicroOps = 2; }
def M4WriteVLDB    : SchedWriteRes<[M4UnitL,
                                    M4UnitL,
                                    M4UnitL]>     { let Latency = 6;
                                                    let NumMicroOps = 3; }
def M4WriteVLDC    : SchedWriteRes<[M4UnitL,
                                    M4UnitL,
                                    M4UnitL,
                                    M4UnitL]>     { let Latency = 6;
                                                    let NumMicroOps = 4; }
def M4WriteVLDD    : SchedWriteRes<[M4UnitL,
                                    M4UnitNSHF]>  { let Latency = 6;
                                                    let NumMicroOps = 2;
                                                    let ResourceCycles = [2, 1]; }
def M4WriteVLDF    : SchedWriteRes<[M4UnitL,
                                    M4UnitL]>     { let Latency = 10;
                                                    let NumMicroOps = 2;
                                                    let ResourceCycles = [3, 3]; }
def M4WriteVLDG    : SchedWriteRes<[M4UnitL,
                                    M4UnitNSHF,
                                    M4UnitNSHF]>  { let Latency = 6;
                                                    let NumMicroOps = 3;
                                                    let ResourceCycles = [2, 1, 1]; }
def M4WriteVLDI    : SchedWriteRes<[M4UnitL,
                                    M4UnitL,
                                    M4UnitL]>     { let Latency = 12;
                                                    let NumMicroOps = 3;
                                                    let ResourceCycles = [3, 3, 3]; }
def M4WriteVLDJ    : SchedWriteRes<[M4UnitL,
                                    M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitNSHF]>  { let Latency = 7;
                                                    let NumMicroOps = 4;
                                                    let ResourceCycles = [3, 1, 1, 1]; }
def M4WriteVLDK    : SchedWriteRes<[M4UnitL,
                                    M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitNSHF]>  { let Latency = 7;
                                                    let NumMicroOps = 5;
                                                    let ResourceCycles = [3, 1, 1, 1, 1]; }
def M4WriteVLDL    : SchedWriteRes<[M4UnitL,
                                    M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitL,
                                    M4UnitNSHF]>  { let Latency = 7;
                                                    let NumMicroOps = 5;
                                                    let ResourceCycles = [3, 1, 1, 6, 1]; }
def M4WriteVLDM    : SchedWriteRes<[M4UnitL,
                                    M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitL,
                                    M4UnitNSHF,
                                    M4UnitNSHF]>  { let Latency = 7;
                                                    let NumMicroOps = 6;
                                                    let ResourceCycles = [3, 1, 1, 3, 1, 1]; }
def M4WriteVLDN    : SchedWriteRes<[M4UnitL,
                                    M4UnitL,
                                    M4UnitL,
                                    M4UnitL]>     { let Latency = 14;
                                                    let NumMicroOps = 4;
                                                    let ResourceCycles = [3, 3, 3, 3]; }

def M4WriteVST1    : SchedWriteRes<[M4UnitS,
                                    M4UnitFST]>  { let Latency = 1;
                                                   let NumMicroOps = 1; }
def M4WriteVSTA    : WriteSequence<[WriteVST], 2>;
def M4WriteVSTB    : WriteSequence<[WriteVST], 3>;
def M4WriteVSTC    : WriteSequence<[WriteVST], 4>;
def M4WriteVSTD    : SchedWriteRes<[M4UnitS,
                                    M4UnitFST]>   { let Latency = 2; }
def M4WriteVSTE    : SchedWriteRes<[M4UnitS,
                                    M4UnitFST,
                                    M4UnitS,
                                    M4UnitFST]>   { let Latency = 2;
                                                    let NumMicroOps = 2; }
def M4WriteVSTF    : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitS,
                                    M4UnitFST,
                                    M4UnitS,
                                    M4UnitFST]>   { let Latency = 4;
                                                    let NumMicroOps = 4;
                                                    let ResourceCycles = [1, 2, 1, 2, 1]; }
def M4WriteVSTG    : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitS,
                                    M4UnitFST,
                                    M4UnitS,
                                    M4UnitFST,
                                    M4UnitS,
                                    M4UnitFST]>   { let Latency = 5;
                                                    let NumMicroOps = 6;
                                                    let ResourceCycles = [1, 1, 1, 2, 1, 2, 1, 2, 1]; }
def M4WriteVSTI    : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitS,
                                    M4UnitFST,
                                    M4UnitS,
                                    M4UnitFST,
                                    M4UnitS,
                                    M4UnitFST,
                                    M4UnitS,
                                    M4UnitFST]>   { let Latency = 8;
                                                    let NumMicroOps = 5;
                                                    let ResourceCycles = [1, 1, 1, 1, 2, 1, 2, 1, 2, 1, 2, 1]; }
def M4WriteVSTJ    : SchedWriteRes<[M4UnitA,
                                    M4UnitS,
                                    M4UnitFST]>   { let Latency = 1;
                                                    let NumMicroOps = 2; }
def M4WriteVSTK    : SchedWriteRes<[M4UnitA,
                                    M4UnitS,
                                    M4UnitFST]>   { let Latency = 3;
                                                    let NumMicroOps = 2; }
def M4WriteVSTL    : SchedWriteRes<[M4UnitNSHF,
                                    M4UnitNSHF,
                                    M4UnitS,
                                    M4UnitFST,
                                    M4UnitS,
                                    M4UnitFST]>   { let Latency = 4;
                                                    let NumMicroOps = 4;
                                                    let ResourceCycles = [1, 1, 2, 1, 2, 1]; }

// Special cases.
def M4WriteCOPY    : SchedWriteVariant<[SchedVar<ExynosFPPred, [M4WriteNALU1]>,
                                        SchedVar<NoSchedPred,  [M4WriteZ0]>]>;
def M4WriteMOVI    : SchedWriteVariant<[SchedVar<IsZeroFPIdiomPred, [M4WriteZ0]>,
                                        SchedVar<NoSchedPred,       [M4WriteNALU1]>]>;
def M4WriteMULL    : SchedWriteVariant<[SchedVar<ExynosLongVectorUpperPred, [M4WriteNEONM]>,
                                        SchedVar<NoSchedPred,               [M4WriteNMUL3]>]>;

// Fast forwarding.
def M4ReadAESM1    : SchedReadAdvance<+1, [M4WriteNCRY1]>;
def M4ReadFMACM1   : SchedReadAdvance<+1, [M4WriteFMAC4,
                                           M4WriteFMAC4H,
                                           M4WriteFMAC5]>;
def M4ReadNMULM1   : SchedReadAdvance<+1, [M4WriteNMUL3]>;
def M4ReadMULLP2   : SchedReadAdvance<-2, [M4WriteNEONM]>;

//===----------------------------------------------------------------------===//
// Coarse scheduling model.

// Branch instructions.
def : SchedAlias<WriteBr,    M4WriteZ0>;
def : SchedAlias<WriteBrReg, M4WriteC1>;

// Arithmetic and logical integer instructions.
def : SchedAlias<WriteI,     M4WriteA1>;
def : SchedAlias<WriteIEReg, M4WriteAA>; // FIXME: M4WriteAX crashes TableGen.
def : SchedAlias<WriteISReg, M4WriteAA>; // FIXME: M4WriteAX crashes TableGen.
def : SchedAlias<WriteIS,    M4WriteA1>;

// Move instructions.
def : SchedAlias<WriteImm, M4WriteA1>;

// Divide and multiply instructions.
def : SchedAlias<WriteID32, M4WriteD12>;
def : SchedAlias<WriteID64, M4WriteD21>;
def : SchedAlias<WriteIM32, M4WriteC3>;
def : SchedAlias<WriteIM64, M4WriteCA>;

// Miscellaneous instructions.
def : SchedAlias<WriteExtr, M4WriteAY>;

// Addressing modes.
def : SchedAlias<WriteAdr,    M4WriteZ1>;
def : SchedAlias<ReadAdrBase, M4ReadAdrBase>;

// Load instructions.
def : SchedAlias<WriteLD,    M4WriteL4>;
def : SchedAlias<WriteLDHi,  M4WriteZ4>;
def : SchedAlias<WriteLDIdx, M4WriteLX>;

// Store instructions.
def : SchedAlias<WriteST,    M4WriteS1>;
def : SchedAlias<WriteSTP,   M4WriteS1>;
def : SchedAlias<WriteSTX,   M4WriteS1>;
def : SchedAlias<WriteSTIdx, M4WriteSX>;

// FP data instructions.
def : SchedAlias<WriteF,    M4WriteFADD2>;
def : SchedAlias<WriteFCmp, M4WriteNMSC2>;
def : SchedAlias<WriteFDiv, M4WriteFDIV12>;
def : SchedAlias<WriteFMul, M4WriteFMAC3>;

// FP miscellaneous instructions.
def : SchedAlias<WriteFCvt,  M4WriteFCVT2>;
def : SchedAlias<WriteFImm,  M4WriteNALU1>;
def : SchedAlias<WriteFCopy, M4WriteCOPY>;

// FP load instructions.
def : SchedAlias<WriteVLD, M4WriteL5>;

// FP store instructions.
def : SchedAlias<WriteVST, M4WriteVST1>;

// ASIMD FP instructions.
def : SchedAlias<WriteV, M4WriteNALU1>;

// Other miscellaneous instructions.
def : WriteRes<WriteAtomic,  []> { let Unsupported = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }
def : WriteRes<WriteSys,     []> { let Latency = 1; }

//===----------------------------------------------------------------------===//
// Generic fast forwarding.

// TODO: Add FP register forwarding rules.

def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
// TODO: The forwarding for 32 bits actually saves 2 cycles.
def : ReadAdvance<ReadIMA,     3, [WriteIM32, WriteIM64]>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadVLD,     0>;

//===----------------------------------------------------------------------===//
// Finer scheduling model.

// Branch instructions
def : InstRW<[M4WriteB1], (instrs Bcc)>;
def : InstRW<[M4WriteAF], (instrs BL)>;
def : InstRW<[M4WriteBX], (instrs BLR)>;
def : InstRW<[M4WriteC1], (instregex "^CBN?Z[WX]")>;
def : InstRW<[M4WriteAD], (instregex "^TBN?Z[WX]")>;

// Arithmetic and logical integer instructions.
def : InstRW<[M4WriteAX], (instregex "^(ADD|AND|BIC|EON|EOR|ORN|SUB)[WX]rs$")>;
def : InstRW<[M4WriteAU], (instrs ORRWrs, ORRXrs)>;
def : InstRW<[M4WriteAX], (instregex "^(ADD|AND|BIC|SUB)S[WX]rs$")>;
def : InstRW<[M4WriteAX], (instregex "^(ADD|SUB)S?[WX]rx(64)?$")>;
def : InstRW<[M4WriteAV], (instrs ADDWri, ADDXri, ORRWri, ORRXri)>;

// Move instructions.
def : InstRW<[M4WriteCOPY], (instrs COPY)>;
def : InstRW<[M4WriteZ0],   (instrs ADR, ADRP)>;
def : InstRW<[M4WriteZ0],   (instregex "^MOV[NZ][WX]i")>;

// Divide and multiply instructions.

// Miscellaneous instructions.

// Load instructions.
def : InstRW<[M4WriteLD,
              WriteLDHi,
              WriteAdr],    (instregex "^LDP(SW|W|X)(post|pre)")>;
def : InstRW<[M4WriteL5,
              ReadAdrBase], (instregex "^LDR(BB|SBW|SBX|HH|SHW|SHX|SW|W|X)roW")>;
def : InstRW<[WriteLDIdx,
              ReadAdrBase], (instregex "^LDR(BB|SBW|SBX|HH|SHW|SHX|SW|W|X)roX")>;
def : InstRW<[M4WriteL5,
              ReadAdrBase], (instrs PRFMroW)>;
def : InstRW<[WriteLDIdx,
              ReadAdrBase], (instrs PRFMroX)>;

// Store instructions.
def : InstRW<[M4WriteSB,
              ReadAdrBase], (instregex "^STR(BB|HH|W|X)roW")>;
def : InstRW<[WriteST,
              ReadAdrBase], (instregex "^STR(BB|HH|W|X)roX")>;

// FP data instructions.
def : InstRW<[M4WriteNSHF1H], (instrs FABSHr)>;
def : InstRW<[M4WriteNSHF1],  (instregex "^FABS[SD]r")>;
def : InstRW<[M4WriteFADD2H], (instregex "^F(ADD|SUB)Hrr")>;
def : InstRW<[M4WriteFADD2],  (instregex "^F(ADD|SUB)[SD]rr")>;
def : InstRW<[M4WriteFADD2H], (instregex "^FADDPv.i16")>;
def : InstRW<[M4WriteFADD2],  (instregex "^FADDPv.i(32|64)")>;
def : InstRW<[M4WriteNEONQ],  (instregex "^FCCMPE?[HSD]rr")>;
def : InstRW<[M4WriteNMSC2],  (instregex "^FCMPE?[HSD]r[ir]")>;
def : InstRW<[M4WriteNMSC1],  (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)(16|32|64|v1)")>;
def : InstRW<[M4WriteFDIV7H], (instrs FDIVHrr)>;
def : InstRW<[M4WriteFDIV7],  (instrs FDIVSrr)>;
def : InstRW<[M4WriteFDIV12], (instrs FDIVDrr)>;
def : InstRW<[M4WriteNMSC1],  (instregex "^F(MAX|MIN)(NM)?[HSD]rr")>;
def : InstRW<[M4WriteFMAC3H], (instregex "^FN?MULHrr")>;
def : InstRW<[M4WriteFMAC3],  (instregex "^FN?MUL[SD]rr")>;
def : InstRW<[M4WriteFMAC3H], (instrs FMULX16)>;
def : InstRW<[M4WriteFMAC3],  (instregex "^FMULX(32|64)")>;
def : InstRW<[M4WriteFMAC4H,
              M4ReadFMACM1],  (instregex "^FN?M(ADD|SUB)Hrrr")>;
def : InstRW<[M4WriteFMAC4,
              M4ReadFMACM1],  (instregex "^FN?M(ADD|SUB)[SD]rrr")>;
def : InstRW<[M4WriteNALU1H], (instrs FNEGHr)>;
def : InstRW<[M4WriteNALU1],  (instregex "^FNEG[SD]r")>;
def : InstRW<[M4WriteFCVT3A], (instregex "^FRINT.+r")>;
def : InstRW<[M4WriteNEONH],  (instregex "^FCSEL[HSD]rrr")>;
def : InstRW<[M4WriteFSQR7H], (instrs FSQRTHr)>;
def : InstRW<[M4WriteFSQR8],  (instrs FSQRTSr)>;
def : InstRW<[M4WriteFSQR12], (instrs FSQRTDr)>;

// FP miscellaneous instructions.
def : InstRW<[M4WriteFCVT2H], (instregex "^FCVTH[SD]r")>;
def : InstRW<[M4WriteFCVT2H], (instregex "^FCVT[SD]Hr")>;
def : InstRW<[M4WriteFCVT2],  (instregex "^FCVT[SD][SD]r")>;
def : InstRW<[M4WriteFCVT6A], (instregex "^[SU]CVTF[SU][XW][HSD]ri")>;
def : InstRW<[M4WriteNEONR],  (instregex "^FCVT[AMNPZ][SU][SU][XW][HSD]r")>;
def : InstRW<[M4WriteNALU1],  (instregex "^FMOV[HSD][ir]")>;
def : InstRW<[M4WriteSA],     (instregex "^FMOV[WX][HSD]r")>;
def : InstRW<[M4WriteNEONJ],  (instregex "^FMOV[HSD][WX]r")>;
def : InstRW<[M4WriteNEONI],  (instregex "^FMOVXDHighr")>;
def : InstRW<[M4WriteNEONK],  (instregex "^FMOVDXHighr")>;
def : InstRW<[M4WriteFCVT3H], (instregex "^F(RECP|RSQRT)Ev1f16")>;
def : InstRW<[M4WriteFCVT3],  (instregex "^F(RECP|RSQRT)Ev1i(32|64)")>;
def : InstRW<[M4WriteNMSC1],  (instregex "^FRECPXv1")>;
def : InstRW<[M4WriteFMAC4H,
              M4ReadFMACM1],  (instregex "^F(RECP|RSQRT)S16")>;
def : InstRW<[M4WriteFMAC4,
              M4ReadFMACM1],  (instregex "^F(RECP|RSQRT)S(32|64)")>;

// FP load instructions.
def : InstRW<[WriteVLD],    (instregex "^LDR[SDQ]l")>;
def : InstRW<[WriteVLD],    (instregex "^LDUR[BHSDQ]i")>;
def : InstRW<[WriteVLD,
              WriteAdr],    (instregex "^LDR[BHSDQ](post|pre)")>;
def : InstRW<[WriteVLD],    (instregex "^LDR[BHSDQ]ui")>;
def : InstRW<[M4WriteLE,
              ReadAdrBase], (instregex "^LDR[BHSDQ]roW")>;
def : InstRW<[WriteVLD,
              ReadAdrBase], (instregex "^LDR[BHSD]roX")>;
def : InstRW<[M4WriteLE,
              ReadAdrBase], (instrs LDRQroX)>;
def : InstRW<[WriteVLD,
              M4WriteLH],   (instregex "^LDN?P[SD]i")>;
def : InstRW<[M4WriteLA,
              M4WriteLH],   (instregex "^LDN?PQi")>;
def : InstRW<[M4WriteL5,
              M4WriteLH,
              WriteAdr],    (instregex "^LDP[SD]post")>;
def : InstRW<[M4WriteLB,
              M4WriteLH,
              WriteAdr],    (instrs LDPQpost)>;
def : InstRW<[M4WriteLB,
              M4WriteLH,
              WriteAdr],    (instregex "^LDP[SD]pre")>;
def : InstRW<[M4WriteLC,
              M4WriteLH,
              WriteAdr],    (instrs LDPQpre)>;

// FP store instructions.
def : InstRW<[WriteVST],    (instregex "^STUR[BHSDQ]i")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "^STR[BHSDQ](post|pre)")>;
def : InstRW<[WriteVST],    (instregex "^STR[BHSDQ]ui")>;
def : InstRW<[M4WriteVSTJ,
              ReadAdrBase], (instregex "^STR[BHSD]roW")>;
def : InstRW<[M4WriteVSTK,
              ReadAdrBase], (instrs STRQroW)>;
def : InstRW<[WriteVST,
              ReadAdrBase], (instregex "^STR[BHSD]roX")>;
def : InstRW<[M4WriteVSTK,
              ReadAdrBase], (instrs STRQroX)>;
def : InstRW<[WriteVST],    (instregex "^STN?P[SD]i")>;
def : InstRW<[M4WriteVSTA], (instregex "^STN?PQi")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "^STP[SD](post|pre)")>;
def : InstRW<[M4WriteVSTJ,
              WriteAdr],    (instregex "^STPQ(post|pre)")>;

// ASIMD instructions.
def : InstRW<[M4WriteNHAD1],  (instregex "^[SU]ABDL?v")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU]ABAL?v")>;
def : InstRW<[M4WriteNMSC1],  (instregex "^ABSv")>;
def : InstRW<[M4WriteNALU1],  (instregex "^(ADD|NEG|SUB)v")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU]?ADDL?Pv")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU]H(ADD|SUB)v")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU](ADD|SUB)[LW]v")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^R?(ADD|SUB)HN2?v")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU]Q(ADD|SUB)v")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^(SU|US)QADDv")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU]RHADDv")>;
def : InstRW<[M4WriteNMSC1],  (instregex "^SQ(ABS|NEG)v")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU]?ADDL?Vv")>;
def : InstRW<[M4WriteNMSC1],  (instregex "^CM(EQ|GE|GT|HI|HS|LE|LT)v")>;
def : InstRW<[M4WriteNALU1],  (instregex "^CMTSTv")>;
def : InstRW<[M4WriteNALU1],  (instregex "^(AND|BIC|EOR|NOT|ORN|ORR)v")>;
def : InstRW<[M4WriteNMSC1],  (instregex "^[SU](MIN|MAX)v")>;
def : InstRW<[M4WriteNMSC2],  (instregex "^[SU](MIN|MAX)Pv")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU](MIN|MAX)Vv")>;
def : InstRW<[M4WriteNMUL3],  (instregex "^(SQR?D)?MULH?v")>;
def : InstRW<[M4WriteNMUL3,
              M4ReadNMULM1],  (instregex "^ML[AS]v")>;
def : InstRW<[M4WriteNMUL3],  (instregex "^SQRDML[AS]H")>;
def : InstRW<[M4WriteMULL,
              M4ReadMULLP2],  (instregex "^(S|U|SQD)ML[AS]Lv")>;
def : InstRW<[M4WriteMULL,
              M4ReadMULLP2],  (instregex "^(S|U|SQD)MULLv")>;
def : InstRW<[M4WriteNMUL3],  (instregex "^[SU]DOT(lane)?v")>;
def : InstRW<[M4WriteNHAD3],  (instregex "^[SU]ADALPv")>;
def : InstRW<[M4WriteNSHT4A], (instregex "^[SU]R?SRA[dv]")>;
def : InstRW<[M4WriteNSHT1],  (instregex "^SHL[dv]")>;
def : InstRW<[M4WriteNSHT1],  (instregex "^S[LR]I[dv]")>;
def : InstRW<[M4WriteNSHT1],  (instregex "^[SU]SH[LR][dv]")>;
def : InstRW<[M4WriteNSHT2],  (instregex "^[SU]?SHLLv")>;
def : InstRW<[M4WriteNSHT4A], (instregex "^[SU]?Q?R?SHRU?N[bhsv]")>;
def : InstRW<[M4WriteNSHT4A], (instregex "^[SU]RSH[LR][dv]")>;
def : InstRW<[M4WriteNSHT4A], (instregex "^[SU]QR?SHLU?[bhsdv]")>;

// ASIMD FP instructions.
def : InstRW<[M4WriteNSHF1H], (instregex "^FABSv.f16")>;
def : InstRW<[M4WriteNSHF1],  (instregex "^FABSv.f(32|64)")>;
def : InstRW<[M4WriteFADD2H], (instregex "^F(ABD|ADD|SUB)v.f16")>;
def : InstRW<[M4WriteFADD2],  (instregex "^F(ABD|ADD|SUB)v.f(32|64)")>;
def : InstRW<[M4WriteFADD2H], (instregex "^FADDPv.f16")>;
def : InstRW<[M4WriteFADD2],  (instregex "^FADDPv.f(32|64)")>;
def : InstRW<[M4WriteNMSC1],  (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v[^1]")>;
def : InstRW<[M4WriteFCVT2],  (instregex "^FCVT(L|N|XN)v")>;
def : InstRW<[M4WriteFCVT2A], (instregex "^FCVT[AMNPZ][SU]v")>;
def : InstRW<[M4WriteFCVT2H], (instregex "^[SU]CVTFv.[fi]16")>;
def : InstRW<[M4WriteFCVT2],  (instregex "^[SU]CVTFv.[fi](32|64)")>;
def : InstRW<[M4WriteFDIV7H], (instrs FDIVv4f16)>;
def : InstRW<[M4WriteNEONVH], (instrs FDIVv8f16)>;
def : InstRW<[M4WriteFDIV7],  (instrs FDIVv2f32)>;
def : InstRW<[M4WriteNEONV],  (instrs FDIVv4f32)>;
def : InstRW<[M4WriteNEONW],  (instrs FDIVv2f64)>;
def : InstRW<[M4WriteNMSC1],  (instregex "^F(MAX|MIN)(NM)?v")>;
def : InstRW<[M4WriteNMSC2],  (instregex "^F(MAX|MIN)(NM)?Pv")>;
def : InstRW<[M4WriteNEONZ],  (instregex "^F(MAX|MIN)(NM)?Vv")>;
def : InstRW<[M4WriteFMAC2H], (instregex "^FMULX?v.[fi]16")>;
def : InstRW<[M4WriteFMAC3],  (instregex "^FMULX?v.[fi](32|64)")>;
def : InstRW<[M4WriteFMAC4H,
              M4ReadFMACM1],  (instregex "^FML[AS]v.[fi]16")>;
def : InstRW<[M4WriteFMAC4,
              M4ReadFMACM1],  (instregex "^FML[AS]v.[fi](32|64)")>;
def : InstRW<[M4WriteNALU1H], (instregex "^FNEGv.f16")>;
def : InstRW<[M4WriteNALU1],  (instregex "^FNEGv.f(32|64)")>;
def : InstRW<[M4WriteFCVT3A], (instregex "^FRINT[AIMNPXZ]v")>;
def : InstRW<[M4WriteFSQR7H], (instrs FSQRTv4f16)>;
def : InstRW<[M4WriteNEONXH], (instrs FSQRTv8f16)>;
def : InstRW<[M4WriteFSQR8],  (instrs FSQRTv2f32)>;
def : InstRW<[M4WriteNEONX],  (instrs FSQRTv4f32)>;
def : InstRW<[M4WriteNEONY],  (instrs FSQRTv2f64)>;

// ASIMD miscellaneous instructions.
def : InstRW<[M4WriteNALU1],  (instregex "^RBITv")>;
def : InstRW<[M4WriteNALU1],  (instregex "^(BIF|BIT|BSL)v")>;
def : InstRW<[M4WriteNALU1],  (instregex "^CL[STZ]v")>;
def : InstRW<[M4WriteNEONB],  (instregex "^DUPv.+gpr")>;
def : InstRW<[M4WriteNSHF1],  (instregex "^CPY")>;
def : InstRW<[M4WriteNSHF1],  (instregex "^DUPv.+lane")>;
def : InstRW<[M4WriteNSHF1],  (instregex "^EXTv")>;
def : InstRW<[M4WriteNSHT4A], (instregex "^XTNv")>;
def : InstRW<[M4WriteNSHT4A], (instregex "^[SU]?QXTU?Nv")>;
def : InstRW<[M4WriteNEONB],  (instregex "^INSv.+gpr")>;
def : InstRW<[M4WriteNSHF1],  (instregex "^INSv.+lane")>;
def : InstRW<[M4WriteMOVI],   (instregex "^(MOV|MVN)I")>;
def : InstRW<[M4WriteNALU1H], (instregex "^FMOVv.f16")>;
def : InstRW<[M4WriteNALU1],  (instregex "^FMOVv.f(32|64)")>;
def : InstRW<[M4WriteFCVT3H], (instregex "^F(RECP|RSQRT)Ev[248]f16")>;
def : InstRW<[M4WriteFCVT3],  (instregex "^F(RECP|RSQRT)Ev[248]f(32|64)")>;
def : InstRW<[M4WriteFCVT3],  (instregex "^U(RECP|RSQRT)Ev[24]i32")>;
def : InstRW<[M4WriteFMAC4H,
              M4ReadFMACM1],  (instregex "^F(RECP|RSQRT)Sv.f16")>;
def : InstRW<[M4WriteFMAC4,
              M4ReadFMACM1],  (instregex "^F(RECP|RSQRT)Sv.f(32|64)")>;
def : InstRW<[M4WriteNSHF1],  (instregex "^REV(16|32|64)v")>;
def : InstRW<[M4WriteNSHFA],  (instregex "^TB[LX]v(8|16)i8One")>;
def : InstRW<[M4WriteNSHFB],  (instregex "^TB[LX]v(8|16)i8Two")>;
def : InstRW<[M4WriteNSHFC],  (instregex "^TB[LX]v(8|16)i8Three")>;
def : InstRW<[M4WriteNSHFD],  (instregex "^TB[LX]v(8|16)i8Four")>;
def : InstRW<[M4WriteNEONP],  (instregex "^[SU]MOVv")>;
def : InstRW<[M4WriteNSHF1],  (instregex "^(TRN|UZP|ZIP)[12]v")>;

// ASIMD load instructions.
def : InstRW<[WriteVLD],    (instregex "LD1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVLD,
              M4WriteA1],   (instregex "LD1Onev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteVLD],    (instregex "LD1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLD,
              M4WriteA1],   (instregex "LD1Onev(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDA], (instregex "LD1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVLDA,
              M4WriteA1],   (instregex "LD1Twov(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVLDA], (instregex "LD1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDA,
              M4WriteA1],   (instregex "LD1Twov(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDB], (instregex "LD1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVLDB,
              M4WriteA1],   (instregex "LD1Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVLDB], (instregex "LD1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDB,
              M4WriteA1],   (instregex "LD1Threev(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDC], (instregex "LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVLDC,
              M4WriteA1],   (instregex "LD1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVLDC], (instregex "LD1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDC,
              M4WriteA1],   (instregex "LD1Fourv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDD], (instregex "LD1i(8|16|32|64)$")>;
def : InstRW<[M4WriteVLDD,
              M4WriteA1],   (instregex "LD1i(8|16|32|64)_POST$")>;

def : InstRW<[WriteVLD],    (instregex "LD1Rv(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVLD,
              M4WriteA1],   (instregex "LD1Rv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteVLD],    (instregex "LD1Rv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLD,
              M4WriteA1],   (instregex "LD1Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDF], (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[M4WriteVLDF,
              M4WriteA1],   (instregex "LD2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[M4WriteVLDF], (instregex "LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDF,
              M4WriteA1],   (instregex "LD2Twov(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDG], (instregex "LD2i(8|16|32|64)$")>;
def : InstRW<[M4WriteVLDG,
              M4WriteA1],   (instregex "LD2i(8|16|32|64)_POST$")>;

def : InstRW<[M4WriteVLDA], (instregex "LD2Rv(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVLDA,
              M4WriteA1],   (instregex "LD2Rv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVLDA], (instregex "LD2Rv(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDA,
              M4WriteA1],   (instregex "LD2Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDI], (instregex "LD3Threev(8b|4h|2s)$")>;
def : InstRW<[M4WriteVLDI,
              M4WriteA1],   (instregex "LD3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[M4WriteVLDI], (instregex "LD3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDI,
              M4WriteA1],   (instregex "LD3Threev(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDJ], (instregex "LD3i(8|16|32)$")>;
def : InstRW<[M4WriteVLDJ,
              M4WriteA1],   (instregex "LD3i(8|16|32)_POST$")>;
def : InstRW<[M4WriteVLDL], (instregex "LD3i64$")>;
def : InstRW<[M4WriteVLDL,
              M4WriteA1],   (instregex "LD3i64_POST$")>;

def : InstRW<[M4WriteVLDB], (instregex "LD3Rv(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVLDB,
              M4WriteA1],   (instregex "LD3Rv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVLDB], (instregex "LD3Rv(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDB,
              M4WriteA1],   (instregex "LD3Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDN], (instregex "LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[M4WriteVLDN,
              M4WriteA1],   (instregex "LD4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[M4WriteVLDN], (instregex "LD4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDN,
              M4WriteA1],   (instregex "LD4Fourv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVLDK], (instregex "LD4i(8|16|32)$")>;
def : InstRW<[M4WriteVLDK,
              M4WriteA1],   (instregex "LD4i(8|16|32)_POST$")>;
def : InstRW<[M4WriteVLDM], (instregex "LD4i64$")>;
def : InstRW<[M4WriteVLDM,
              M4WriteA1],   (instregex "LD4i64_POST$")>;

def : InstRW<[M4WriteVLDC], (instregex "LD4Rv(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVLDC,
              M4WriteA1],   (instregex "LD4Rv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVLDC], (instregex "LD4Rv(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVLDC,
              M4WriteA1],   (instregex "LD4Rv(16b|8h|4s|2d)_POST$")>;

// ASIMD store instructions.
def : InstRW<[WriteVST],    (instregex "ST1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVST,
              M4WriteA1],   (instregex "ST1Onev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteVST],    (instregex "ST1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVST,
              M4WriteA1],   (instregex "ST1Onev(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVSTA], (instregex "ST1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVSTA,
              M4WriteA1],   (instregex "ST1Twov(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVSTA], (instregex "ST1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVSTA,
              M4WriteA1],   (instregex "ST1Twov(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVSTB], (instregex "ST1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVSTB,
              M4WriteA1],   (instregex "ST1Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVSTB], (instregex "ST1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVSTB,
              M4WriteA1],   (instregex "ST1Threev(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVSTC], (instregex "ST1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[M4WriteVSTC,
              M4WriteA1],   (instregex "ST1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M4WriteVSTC], (instregex "ST1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVSTC,
              M4WriteA1],   (instregex "ST1Fourv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[WriteVST],    (instregex "ST1i(8|16|32|64)$")>;
def : InstRW<[WriteVST,
              M4WriteA1],   (instregex "ST1i(8|16|32|64)_POST$")>;

def : InstRW<[M4WriteVSTD], (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[M4WriteVSTD,
              M4WriteA1],   (instregex "ST2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[M4WriteVSTE], (instregex "ST2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVSTE,
              M4WriteA1],   (instregex "ST2Twov(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVSTD], (instregex "ST2i(8|16|32|64)$")>;
def : InstRW<[M4WriteVSTD,
              M4WriteA1],   (instregex "ST2i(8|16|32|64)_POST$")>;

def : InstRW<[M4WriteVSTF], (instregex "ST3Threev(8b|4h|2s)$")>;
def : InstRW<[M4WriteVSTF,
              M4WriteA1],   (instregex "ST3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[M4WriteVSTG], (instregex "ST3Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVSTG,
              M4WriteA1],   (instregex "ST3Threev(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVSTE], (instregex "ST3i(8|16|32|64)$")>;
def : InstRW<[M4WriteVSTE,
              M4WriteA1],   (instregex "ST3i(8|16|32|64)_POST$")>;

def : InstRW<[M4WriteVSTL], (instregex "ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[M4WriteVSTL,
              M4WriteA1],   (instregex "ST4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[M4WriteVSTI], (instregex "ST4Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M4WriteVSTI,
              M4WriteA1],   (instregex "ST4Fourv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M4WriteVSTE], (instregex "ST4i(8|16|32|64)$")>;
def : InstRW<[M4WriteVSTE,
              M4WriteA1],   (instregex "ST4i(8|16|32|64)_POST$")>;

// Cryptography instructions.
def : InstRW<[M4WriteNCRY1],  (instregex "^AES[DE]")>;
def : InstRW<[M4WriteNCRY1,
              M4ReadAESM1],   (instregex "^AESI?MC")>;
def : InstRW<[M4WriteNCRY1A], (instregex "^PMULv")>;
def : InstRW<[M4WriteNCRY1A], (instregex "^PMULLv(1|8)i")>;
def : InstRW<[M4WriteNCRY3A], (instregex "^PMULLv(2|16)i")>;
def : InstRW<[M4WriteNCRY1A], (instregex "^SHA1([CHMP]|SU[01])")>;
def : InstRW<[M4WriteNCRY1A], (instrs SHA256SU0rr)>;
def : InstRW<[M4WriteNCRY5A], (instrs SHA256SU1rrr)>;
def : InstRW<[M4WriteNCRY5A], (instrs SHA256H2rrr)>;

// CRC instructions.
def : InstRW<[M4WriteE2], (instregex "^CRC32C?[BHWX]rr$")>;

} // SchedModel = ExynosM4Model
