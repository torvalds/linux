//=- AArch64SchedExynosM1.td - Samsung Exynos M1 Sched Defs --*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the machine model for the Samsung Exynos M1 to support
// instruction scheduling and other instruction cost heuristics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// The Exynos-M1 is a traditional superscalar microprocessor with a
// 4-wide in-order stage for decode and dispatch and a wider issue stage.
// The execution units and loads and stores are out-of-order.

def ExynosM1Model : SchedMachineModel {
  let IssueWidth            =  4; // Up to 4 uops per cycle.
  let MicroOpBufferSize     = 96; // ROB size.
  let LoopMicroOpBufferSize = 24; // Based on the instruction queue size.
  let LoadLatency           =  4; // Optimistic load cases.
  let MispredictPenalty     = 14; // Minimum branch misprediction penalty.
  let CompleteModel         =  1; // Use the default model otherwise.

  list<Predicate> UnsupportedFeatures = [HasSVE];
}

//===----------------------------------------------------------------------===//
// Define each kind of processor resource and number available on the Exynos-M1,
// which has 9 pipelines, each with its own queue with out-of-order dispatch.

let SchedModel = ExynosM1Model in {

def M1UnitA  : ProcResource<2>; // Simple integer
def M1UnitC  : ProcResource<1>; // Simple and complex integer
def M1UnitD  : ProcResource<1>; // Integer division (inside C, serialized)
def M1UnitB  : ProcResource<2>; // Branch
def M1UnitL  : ProcResource<1>; // Load
def M1UnitS  : ProcResource<1>; // Store
def M1PipeF0 : ProcResource<1>; // FP #0
let Super = M1PipeF0 in {
  def M1UnitFMAC   : ProcResource<1>; // FP multiplication
  def M1UnitNAL0   : ProcResource<1>; // Simple vector
  def M1UnitNMISC  : ProcResource<1>; // Miscellanea
  def M1UnitFCVT   : ProcResource<1>; // FP conversion
  def M1UnitNCRYPT : ProcResource<1>; // Cryptographic
}
def M1PipeF1 : ProcResource<1>; // FP #1
let Super = M1PipeF1 in {
  def M1UnitFADD : ProcResource<1>; // Simple FP
  def M1UnitNAL1 : ProcResource<1>; // Simple vector
  def M1UnitFVAR : ProcResource<1>; // FP division & square root (serialized)
  def M1UnitFST  : ProcResource<1>; // FP store
}

def M1UnitALU  : ProcResGroup<[M1UnitA,
                               M1UnitC]>;    // All integer
def M1UnitNALU : ProcResGroup<[M1UnitNAL0,
                               M1UnitNAL1]>; // All simple vector

//===----------------------------------------------------------------------===//
// Coarse scheduling model.

def M1WriteA1 : SchedWriteRes<[M1UnitALU]> { let Latency = 1; }
def M1WriteA2 : SchedWriteRes<[M1UnitALU]> { let Latency = 2; }
def M1WriteAA : SchedWriteRes<[M1UnitALU]> { let Latency = 2;
                                             let ResourceCycles = [2]; }
def M1WriteAB : SchedWriteRes<[M1UnitALU,
                               M1UnitC]>   { let Latency = 1;
                                             let NumMicroOps = 2; }
def M1WriteAC : SchedWriteRes<[M1UnitALU,
                               M1UnitALU,
                               M1UnitC]>   { let Latency = 2;
                                             let NumMicroOps = 3; }
def M1WriteAD : SchedWriteRes<[M1UnitALU,
                               M1UnitC]>   { let Latency = 2;
                                             let NumMicroOps = 2; }
def M1WriteAX : SchedWriteVariant<[SchedVar<ExynosArithPred, [M1WriteA1]>,
                                   SchedVar<ExynosLogicPred, [M1WriteA1]>,
                                   SchedVar<NoSchedPred,     [M1WriteAA]>]>;
def M1WriteC1 : SchedWriteRes<[M1UnitC]>   { let Latency = 1; }
def M1WriteC2 : SchedWriteRes<[M1UnitC]>   { let Latency = 2; }

def M1WriteB1 : SchedWriteRes<[M1UnitB]> { let Latency = 1; }
def M1WriteBX : SchedWriteVariant<[SchedVar<ExynosBranchLinkLRPred, [M1WriteAC]>,
                                   SchedVar<NoSchedPred,            [M1WriteAB]>]>;

def M1WriteL5 : SchedWriteRes<[M1UnitL]> { let Latency = 5; }
def M1WriteL6 : SchedWriteRes<[M1UnitL]> { let Latency = 6; }
def M1WriteLA : SchedWriteRes<[M1UnitL]> { let Latency = 6;
                                           let ResourceCycles = [2]; }
def M1WriteLB : SchedWriteRes<[M1UnitL,
                               M1UnitA]> { let Latency = 4;
                                           let NumMicroOps = 2; }
def M1WriteLC : SchedWriteRes<[M1UnitL,
                               M1UnitA]> { let Latency = 5;
                                           let NumMicroOps = 2; }
def M1WriteLD : SchedWriteRes<[M1UnitL,
                               M1UnitA]> { let Latency = 6;
                                           let NumMicroOps = 2;
                                           let ResourceCycles = [2, 1]; }
def M1WriteLH : SchedWriteRes<[]>        { let Latency = 5;
                                           let NumMicroOps = 0; }
def M1WriteLX : SchedWriteVariant<[SchedVar<ScaledIdxPred, [M1WriteLC]>,
                                   SchedVar<NoSchedPred,   [M1WriteL5]>]>;

def M1WriteS1 : SchedWriteRes<[M1UnitS]>   { let Latency = 1; }
def M1WriteS3 : SchedWriteRes<[M1UnitS]>   { let Latency = 3; }
def M1WriteS4 : SchedWriteRes<[M1UnitS]>   { let Latency = 4; }
def M1WriteSA : SchedWriteRes<[M1UnitS,
                               M1UnitFST,
                               M1UnitA]>   { let Latency = 3;
                                             let NumMicroOps = 2; }
def M1WriteSB : SchedWriteRes<[M1UnitS,
                               M1UnitFST,
                               M1UnitS,
                               M1UnitFST,
                               M1UnitA]>   { let Latency = 3;
                                             let NumMicroOps = 3; }
def M1WriteSC : SchedWriteRes<[M1UnitS,
                               M1UnitA]>   { let Latency = 2;
                                             let NumMicroOps = 2; }
def M1WriteSX : SchedWriteVariant<[SchedVar<ScaledIdxPred, [M1WriteSC]>,
                                   SchedVar<NoSchedPred,   [M1WriteS1]>]>;

def M1ReadAdrBase : SchedReadVariant<[SchedVar<ScaledIdxPred, [ReadDefault]>,
                                      SchedVar<NoSchedPred,   [ReadDefault]>]>;

// Branch instructions.
def : WriteRes<WriteBr,    []>        { let Latency = 0; }
def : WriteRes<WriteBrReg, [M1UnitC]> { let Latency = 1; }

// Arithmetic and logical integer instructions.
def : WriteRes<WriteI,     [M1UnitALU]> { let Latency = 1; }
def : WriteRes<WriteISReg, [M1UnitALU]> { let Latency = 1; }
def : WriteRes<WriteIEReg, [M1UnitALU]> { let Latency = 1; }
def : WriteRes<WriteIS,    [M1UnitALU]> { let Latency = 1; }

// Move instructions.
def : WriteRes<WriteImm, [M1UnitALU]> { let Latency = 1; }

// Divide and multiply instructions.
def : WriteRes<WriteID32, [M1UnitC,
                           M1UnitD]> { let Latency = 13;
                                       let ResourceCycles = [1, 13]; }
def : WriteRes<WriteID64, [M1UnitC,
                           M1UnitD]> { let Latency = 21;
                                       let ResourceCycles = [1, 21]; }
// TODO: Long multiplication take 5 cycles and also the ALU.
def : WriteRes<WriteIM32, [M1UnitC]> { let Latency = 3; }
def : WriteRes<WriteIM64, [M1UnitC]> { let Latency = 4;
                                       let ResourceCycles = [2]; }

// Miscellaneous instructions.
def : WriteRes<WriteExtr, [M1UnitALU,
                           M1UnitALU]> { let Latency = 2;
                                         let NumMicroOps = 2; }

// Addressing modes.
def : WriteRes<WriteAdr, []> { let Latency = 1;
                               let NumMicroOps = 0; }
def : SchedAlias<ReadAdrBase, M1ReadAdrBase>;

// Load instructions.
def : WriteRes<WriteLD,    [M1UnitL]>   { let Latency = 4; }
def : WriteRes<WriteLDHi,  []>          { let Latency = 4;
                                          let NumMicroOps = 0; }
def : SchedAlias<WriteLDIdx, M1WriteLX>;

// Store instructions.
def : WriteRes<WriteST,    [M1UnitS]> { let Latency = 1; }
def : WriteRes<WriteSTP,   [M1UnitS]> { let Latency = 1; }
def : WriteRes<WriteSTX,   [M1UnitS]> { let Latency = 1; }
def : SchedAlias<WriteSTIdx, M1WriteSX>;

// FP data instructions.
def : WriteRes<WriteF,    [M1UnitFADD]>  { let Latency = 3; }
def : WriteRes<WriteFCmp, [M1UnitNMISC]> { let Latency = 4; }
def : WriteRes<WriteFDiv, [M1UnitFVAR]>  { let Latency = 15;
                                           let ResourceCycles = [15]; }
def : WriteRes<WriteFMul, [M1UnitFMAC]>  { let Latency = 4; }

// FP miscellaneous instructions.
def : WriteRes<WriteFCvt,  [M1UnitFCVT]> { let Latency = 3; }
def : WriteRes<WriteFImm,  [M1UnitNALU]> { let Latency = 1; }
def : WriteRes<WriteFCopy, [M1UnitS]>    { let Latency = 4; }

// FP load instructions.
def : WriteRes<WriteVLD,   [M1UnitL]> { let Latency = 5; }

// FP store instructions.
def : WriteRes<WriteVST, [M1UnitS,
                          M1UnitFST]> { let Latency = 1;
                                        let NumMicroOps = 1; }

// ASIMD FP instructions.
def : WriteRes<WriteV, [M1UnitFADD]> { let Latency = 3; }

// Other miscellaneous instructions.
def : WriteRes<WriteAtomic,  []> { let Unsupported = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }
def : WriteRes<WriteSys,     []> { let Latency = 1; }

//===----------------------------------------------------------------------===//
// Fast forwarding.

// TODO: Add FP register forwarding rules.
def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
// TODO: The forwarding for WriteIM32 saves actually 2 cycles.
def : ReadAdvance<ReadIMA,     3, [WriteIM32, WriteIM64]>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadVLD,     0>;

//===----------------------------------------------------------------------===//
// Finer scheduling model.

def M1WriteNEONA   : SchedWriteRes<[M1UnitNALU,
                                    M1UnitNALU,
                                    M1UnitFADD]>   { let Latency = 9;
                                                     let NumMicroOps = 3; }
def M1WriteNEONB   : SchedWriteRes<[M1UnitNALU,
                                    M1UnitFST]>    { let Latency = 5;
                                                     let NumMicroOps = 2;}
def M1WriteNEONC   : SchedWriteRes<[M1UnitNALU,
                                    M1UnitFST]>    { let Latency = 6;
                                                     let NumMicroOps = 2; }
def M1WriteNEOND   : SchedWriteRes<[M1UnitNALU,
                                    M1UnitFST,
                                    M1UnitL]>      { let Latency = 10;
                                                     let NumMicroOps = 3; }
def M1WriteNEONE   : SchedWriteRes<[M1UnitFCVT,
                                    M1UnitFST]>    { let Latency = 8;
                                                     let NumMicroOps = 2; }
def M1WriteNEONF   : SchedWriteRes<[M1UnitFCVT,
                                    M1UnitFST,
                                    M1UnitL]>      { let Latency = 13;
                                                     let NumMicroOps = 3; }
def M1WriteNEONG   : SchedWriteRes<[M1UnitNMISC,
                                    M1UnitFST]>    { let Latency = 6;
                                                     let NumMicroOps = 2; }
def M1WriteNEONH   : SchedWriteRes<[M1UnitNALU,
                                    M1UnitFST]>    { let Latency = 3;
                                                     let NumMicroOps = 2; }
def M1WriteNEONI   : SchedWriteRes<[M1UnitFST,
                                    M1UnitL]>      { let Latency = 9;
                                                     let NumMicroOps = 2; }
def M1WriteNEONJ   : SchedWriteRes<[M1UnitNMISC,
                                    M1UnitFMAC]>   { let Latency = 6;
                                                     let NumMicroOps = 2; }
def M1WriteNEONK   : SchedWriteRes<[M1UnitNMISC,
                                    M1UnitFMAC]>   { let Latency = 7;
                                                     let NumMicroOps = 2; }
def M1WriteNEONL   : SchedWriteRes<[M1UnitNALU]>   { let Latency = 2;
                                                     let ResourceCycles = [2]; }
def M1WriteFADD3   : SchedWriteRes<[M1UnitFADD]>   { let Latency = 3; }
def M1WriteFCVT3   : SchedWriteRes<[M1UnitFCVT]>   { let Latency = 3; }
def M1WriteFCVT4   : SchedWriteRes<[M1UnitFCVT]>   { let Latency = 4; }
def M1WriteFMAC4   : SchedWriteRes<[M1UnitFMAC]>   { let Latency = 4; }
def M1WriteFMAC5   : SchedWriteRes<[M1UnitFMAC]>   { let Latency = 5; }
// TODO
def M1WriteFVAR15  : SchedWriteRes<[M1UnitFVAR]>   { let Latency = 15;
                                                     let ResourceCycles = [15]; }
def M1WriteFVAR23  : SchedWriteRes<[M1UnitFVAR]>   { let Latency = 23;
                                                     let ResourceCycles = [23]; }
def M1WriteNALU1   : SchedWriteRes<[M1UnitNALU]>   { let Latency = 1; }
def M1WriteNALU2   : SchedWriteRes<[M1UnitNALU]>   { let Latency = 2; }
def M1WriteNAL11   : SchedWriteRes<[M1UnitNAL1]>   { let Latency = 1; }
def M1WriteNAL12   : SchedWriteRes<[M1UnitNAL1]>   { let Latency = 2; }
def M1WriteNAL13   : SchedWriteRes<[M1UnitNAL1]>   { let Latency = 3; }
def M1WriteNCRYPT1 : SchedWriteRes<[M1UnitNCRYPT]> { let Latency = 1; }
def M1WriteNCRYPT5 : SchedWriteRes<[M1UnitNCRYPT]> { let Latency = 5; }
def M1WriteNMISC1  : SchedWriteRes<[M1UnitNMISC]>  { let Latency = 1; }
def M1WriteNMISC2  : SchedWriteRes<[M1UnitNMISC]>  { let Latency = 2; }
def M1WriteNMISC3  : SchedWriteRes<[M1UnitNMISC]>  { let Latency = 3; }
def M1WriteNMISC4  : SchedWriteRes<[M1UnitNMISC]>  { let Latency = 4; }
def M1WriteTB      : SchedWriteRes<[M1UnitC,
                                    M1UnitALU]>    { let Latency = 2;
                                                     let NumMicroOps = 2; }
def M1WriteVLDA    : SchedWriteRes<[M1UnitL,
                                    M1UnitL]>      { let Latency = 6;
                                                     let NumMicroOps = 2; }
def M1WriteVLDB    : SchedWriteRes<[M1UnitL,
                                    M1UnitL,
                                    M1UnitL]>      { let Latency = 7;
                                                     let NumMicroOps = 3; }
def M1WriteVLDC    : SchedWriteRes<[M1UnitL,
                                    M1UnitL,
                                    M1UnitL,
                                    M1UnitL]>      { let Latency = 8;
                                                     let NumMicroOps = 4; }
def M1WriteVLDD    : SchedWriteRes<[M1UnitL,
                                    M1UnitNALU]>   { let Latency = 7;
                                                     let NumMicroOps = 2;
                                                     let ResourceCycles = [2, 1]; }
def M1WriteVLDE    : SchedWriteRes<[M1UnitL,
                                    M1UnitNALU]>   { let Latency = 6;
                                                     let NumMicroOps = 2; }
def M1WriteVLDF    : SchedWriteRes<[M1UnitL,
                                    M1UnitL]>      { let Latency = 10;
                                                     let NumMicroOps = 2;
                                                     let ResourceCycles = [1, 1]; }
def M1WriteVLDG    : SchedWriteRes<[M1UnitL,
                                    M1UnitNALU,
                                    M1UnitNALU]>   { let Latency = 7;
                                                     let NumMicroOps = 3;
                                                     let ResourceCycles = [2, 1, 1]; }
def M1WriteVLDH    : SchedWriteRes<[M1UnitL,
                                    M1UnitNALU,
                                    M1UnitNALU]>   { let Latency = 6;
                                                     let NumMicroOps = 3; }
def M1WriteVLDI    : SchedWriteRes<[M1UnitL,
                                    M1UnitL,
                                    M1UnitL]>      { let Latency = 12;
                                                     let NumMicroOps = 3;
                                                     let ResourceCycles = [2, 2, 2]; }
def M1WriteVLDJ    : SchedWriteRes<[M1UnitL,
                                    M1UnitNALU,
                                    M1UnitNALU,
                                    M1UnitNALU]>   { let Latency = 9;
                                                     let NumMicroOps = 4;
                                                     let ResourceCycles = [2, 1, 1, 1]; }
def M1WriteVLDK    : SchedWriteRes<[M1UnitL,
                                    M1UnitNALU,
                                    M1UnitNALU,
                                    M1UnitNALU,
                                    M1UnitNALU]>   { let Latency = 9;
                                                     let NumMicroOps = 5;
                                                     let ResourceCycles = [2, 1, 1, 1, 1]; }
def M1WriteVLDL    : SchedWriteRes<[M1UnitL,
                                    M1UnitNALU,
                                    M1UnitNALU,
                                    M1UnitL,
                                    M1UnitNALU]>   { let Latency = 7;
                                                     let NumMicroOps = 5;
                                                     let ResourceCycles = [1, 1, 1, 1, 1]; }
def M1WriteVLDM    : SchedWriteRes<[M1UnitL,
                                    M1UnitNALU,
                                    M1UnitNALU,
                                    M1UnitL,
                                    M1UnitNALU,
                                    M1UnitNALU]>   { let Latency = 7;
                                                     let NumMicroOps = 6;
                                                     let ResourceCycles = [1, 1, 1, 1, 1, 1]; }
def M1WriteVLDN    : SchedWriteRes<[M1UnitL,
                                    M1UnitL,
                                    M1UnitL,
                                    M1UnitL]>      { let Latency = 14;
                                                     let NumMicroOps = 4;
                                                     let ResourceCycles = [2, 1, 2, 1]; }
def M1WriteVSTA    : WriteSequence<[WriteVST], 2>;
def M1WriteVSTB    : WriteSequence<[WriteVST], 3>;
def M1WriteVSTC    : WriteSequence<[WriteVST], 4>;
def M1WriteVSTD    : SchedWriteRes<[M1UnitS,
                                    M1UnitFST,
                                    M1UnitFST]>    { let Latency = 7;
                                                     let NumMicroOps = 2;
                                                     let ResourceCycles = [7, 1, 1]; }
def M1WriteVSTE    : SchedWriteRes<[M1UnitS,
                                    M1UnitFST,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitFST]>    { let Latency = 8;
                                                     let NumMicroOps = 3;
                                                     let ResourceCycles = [7, 1, 1, 1, 1]; }
def M1WriteVSTF    : SchedWriteRes<[M1UnitNALU,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitFST,
                                    M1UnitFST]>     { let Latency = 15;
                                                      let NumMicroOps = 5;
                                                      let ResourceCycles = [1, 7, 1, 7, 1, 1, 1]; }
def M1WriteVSTG    : SchedWriteRes<[M1UnitNALU,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitFST,
                                    M1UnitFST]>     { let Latency = 16;
                                                      let NumMicroOps = 6;
                                                      let ResourceCycles = [1, 7, 1, 7, 1, 1, 1, 1, 1]; }
def M1WriteVSTH    : SchedWriteRes<[M1UnitNALU,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitFST,
                                    M1UnitFST]>     { let Latency = 14;
                                                      let NumMicroOps = 4;
                                                      let ResourceCycles = [1, 7, 1, 7, 1]; }
def M1WriteVSTI    : SchedWriteRes<[M1UnitNALU,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitS,
                                    M1UnitFST,
                                    M1UnitFST,
                                    M1UnitFST]>     { let Latency = 17;
                                                      let NumMicroOps = 7;
                                                      let ResourceCycles = [1, 7, 1, 7, 1, 1, 1, 1, 1, 1, 1]; }

// Special cases.
def M1WriteAES  : SchedWriteRes<[M1UnitNCRYPT]>     { let Latency = 1; }
def M1WriteCOPY : SchedWriteVariant<[SchedVar<ExynosFPPred, [M1WriteNALU1]>,
                                     SchedVar<NoSchedPred,  [M1WriteA1]>]>;

// Fast forwarding.
def M1ReadAES : SchedReadAdvance<1, [M1WriteAES]>;

// Branch instructions
def : InstRW<[M1WriteB1], (instrs Bcc)>;
def : InstRW<[M1WriteA1], (instrs BL)>;
def : InstRW<[M1WriteBX], (instrs BLR)>;
def : InstRW<[M1WriteC1], (instregex "^CBN?Z[WX]")>;
def : InstRW<[M1WriteAD], (instregex "^TBN?Z[WX]")>;

// Arithmetic and logical integer instructions.
def : InstRW<[M1WriteAX], (instregex ".+rx(64)?$")>;
def : InstRW<[M1WriteAX], (instregex ".+rs$")>;

// Move instructions.
def : InstRW<[M1WriteCOPY], (instrs COPY)>;

// Divide and multiply instructions.

// Miscellaneous instructions.

// Load instructions.
def : InstRW<[M1WriteLB,
              WriteLDHi,
              WriteAdr],    (instregex "^LDP(SW|W|X)(post|pre)")>;
def : InstRW<[M1WriteLC,
              ReadAdrBase], (instregex "^LDR(BB|SBW|SBX|HH|SHW|SHX|SW|W|X)roW")>;
def : InstRW<[M1WriteL5,
              ReadAdrBase], (instregex "^LDR(BB|SBW|SBX|HH|SHW|SHX|SW|W|X)roX")>;
def : InstRW<[M1WriteLC,
              ReadAdrBase], (instrs PRFMroW)>;
def : InstRW<[M1WriteL5,
              ReadAdrBase], (instrs PRFMroX)>;

// Store instructions.
def : InstRW<[M1WriteSC,
              ReadAdrBase], (instregex "^STR(BB|HH|W|X)roW")>;
def : InstRW<[WriteST,
              ReadAdrBase], (instregex "^STR(BB|HH|W|X)roX")>;

// FP data instructions.
def : InstRW<[M1WriteNALU1],  (instregex "^F(ABS|NEG)[DS]r")>;
def : InstRW<[M1WriteFADD3],  (instregex "^F(ADD|SUB)[DS]rr")>;
def : InstRW<[M1WriteNEONG],  (instregex "^FCCMPE?[DS]rr")>;
def : InstRW<[M1WriteNMISC4], (instregex "^FCMPE?[DS]r")>;
def : InstRW<[M1WriteFVAR15], (instrs FDIVSrr)>;
def : InstRW<[M1WriteFVAR23], (instrs FDIVDrr)>;
def : InstRW<[M1WriteNMISC2], (instregex "^F(MAX|MIN).+rr")>;
def : InstRW<[M1WriteFMAC4],  (instregex "^FN?MUL[DS]rr")>;
def : InstRW<[M1WriteFMAC5],  (instregex "^FN?M(ADD|SUB)[DS]rrr")>;
def : InstRW<[M1WriteFCVT3],  (instregex "^FRINT.+r")>;
def : InstRW<[M1WriteNEONH],  (instregex "^FCSEL[DS]rrr")>;
def : InstRW<[M1WriteFVAR15], (instrs FSQRTSr)>;
def : InstRW<[M1WriteFVAR23], (instrs FSQRTDr)>;

// FP miscellaneous instructions.
def : InstRW<[M1WriteFCVT3],  (instregex "^FCVT[DS][DS]r")>;
def : InstRW<[M1WriteNEONF],  (instregex "^[FSU]CVT[AMNPZ][SU](_Int)?[SU]?[XW]?[DS]?[rds]i?")>;
def : InstRW<[M1WriteNEONE],  (instregex "^[SU]CVTF[SU]")>;
def : InstRW<[M1WriteNALU1],  (instregex "^FMOV[DS][ir]")>;
def : InstRW<[M1WriteFCVT4],  (instregex "^[FU](RECP|RSQRT)Ev1")>;
def : InstRW<[M1WriteNMISC1], (instregex "^FRECPXv1")>;
def : InstRW<[M1WriteFMAC5],  (instregex "^F(RECP|RSQRT)S(16|32|64)")>;
def : InstRW<[M1WriteS4],     (instregex "^FMOV[WX][DS](High)?r")>;
def : InstRW<[M1WriteNEONI],  (instregex "^FMOV[DS][WX](High)?r")>;

// FP load instructions.
def : InstRW<[WriteVLD],    (instregex "^LDR[DSQ]l")>;
def : InstRW<[WriteVLD],    (instregex "^LDUR[BDHSQ]i")>;
def : InstRW<[WriteVLD,
              WriteAdr],    (instregex "^LDR[BDHSQ](post|pre)")>;
def : InstRW<[WriteVLD],    (instregex "^LDR[BDHSQ]ui")>;
def : InstRW<[M1WriteLD,
              ReadAdrBase], (instregex "^LDR[BDHS]roW")>;
def : InstRW<[WriteVLD,
              ReadAdrBase], (instregex "^LDR[BDHS]roX")>;
def : InstRW<[M1WriteLD,
              ReadAdrBase], (instregex "^LDRQro[WX]")>;
def : InstRW<[WriteVLD,
              M1WriteLH],   (instregex "^LDN?P[DS]i")>;
def : InstRW<[M1WriteLA,
              M1WriteLH],   (instregex "^LDN?PQi")>;
def : InstRW<[M1WriteLC,
              M1WriteLH,
              WriteAdr],    (instregex "^LDP[DS](post|pre)")>;
def : InstRW<[M1WriteLD,
              M1WriteLH,
              WriteAdr],    (instregex "^LDPQ(post|pre)")>;

// FP store instructions.
def : InstRW<[WriteVST],    (instregex "^STUR[BDHSQ]i")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "^STR[BDHSQ](post|pre)")>;
def : InstRW<[WriteVST],    (instregex "^STR[BDHSQ]ui")>;
def : InstRW<[M1WriteSA,
              ReadAdrBase], (instregex "^STR[BDHS]roW")>;
def : InstRW<[WriteVST,
              ReadAdrBase], (instregex "^STR[BDHS]roX")>;
def : InstRW<[M1WriteSA,
              ReadAdrBase], (instregex "^STRQro[WX]")>;
def : InstRW<[WriteVST],    (instregex "^STN?P[DSQ]i")>;
def : InstRW<[WriteVST,
              WriteAdr],    (instregex "^STP[DS](post|pre)")>;
def : InstRW<[M1WriteSB,
              WriteAdr],    (instregex "^STPQ(post|pre)")>;

// ASIMD instructions.
def : InstRW<[M1WriteNMISC3], (instregex "^[SU]ABAL?v")>;
def : InstRW<[M1WriteNMISC1], (instregex "^[SU]ABDL?v")>;
def : InstRW<[M1WriteNMISC1], (instregex "^(SQ)?ABSv")>;
def : InstRW<[M1WriteNMISC1], (instregex "^SQNEGv")>;
def : InstRW<[M1WriteNALU1],  (instregex "^(ADD|NEG|SUB)v")>;
def : InstRW<[M1WriteNMISC3], (instregex "^[SU]?H(ADD|SUB)v")>;
def : InstRW<[M1WriteNMISC3], (instregex "^[SU]?AD[AD](L|LP|P|W)V?2?v")>;
def : InstRW<[M1WriteNMISC3], (instregex "^[SU]?SUB[LW]2?v")>;
def : InstRW<[M1WriteNMISC3], (instregex "^R?(ADD|SUB)HN?2?v")>;
def : InstRW<[M1WriteNMISC3], (instregex "^[SU]+Q(ADD|SUB)v")>;
def : InstRW<[M1WriteNMISC3], (instregex "^[SU]RHADDv")>;
def : InstRW<[M1WriteNMISC1], (instregex "^CM(EQ|GE|GT|HI|HS|LE|LT)v")>;
def : InstRW<[M1WriteNALU1],  (instregex "^CMTSTv")>;
def : InstRW<[M1WriteNALU1],  (instregex "^(AND|BIC|EOR|MVNI|NOT|ORN|ORR)v")>;
def : InstRW<[M1WriteNMISC1], (instregex "^[SU](MIN|MAX)v")>;
def : InstRW<[M1WriteNMISC2], (instregex "^[SU](MIN|MAX)Pv")>;
def : InstRW<[M1WriteNMISC3], (instregex "^[SU](MIN|MAX)Vv")>;
def : InstRW<[M1WriteNMISC4], (instregex "^(MUL|SQR?DMULH)v")>;
def : InstRW<[M1WriteNMISC4], (instregex "^ML[AS]v")>;
def : InstRW<[M1WriteNMISC4], (instregex "^(S|U|SQD|SQRD)ML[AS][HL]v")>;
def : InstRW<[M1WriteNMISC4], (instregex "^(S|U|SQD)MULLv")>;
def : InstRW<[M1WriteNAL13],  (instregex "^(S|SR|U|UR)SRAv")>;
def : InstRW<[M1WriteNALU1],  (instregex "^SHL[dv]")>;
def : InstRW<[M1WriteNALU1],  (instregex "^[SU]SH[LR][dv]")>;
def : InstRW<[M1WriteNALU1],  (instregex "^S[RS]I[dv]")>;
def : InstRW<[M1WriteNAL13],  (instregex "^(([SU]Q)?R)?SHRU?N[bhsv]")>;
def : InstRW<[M1WriteNAL13],  (instregex "^[SU]RSH[LR][dv]")>;
def : InstRW<[M1WriteNAL13],  (instregex "^[SU]QR?SHLU?[bdhsv]")>;

// ASIMD FP instructions.
def : InstRW<[M1WriteNALU1],  (instregex "^F(ABS|NEG)v")>;
def : InstRW<[M1WriteNMISC3], (instregex "^F(ABD|ADD|SUB)v")>;
def : InstRW<[M1WriteNEONA],  (instregex "^FADDP")>;
def : InstRW<[M1WriteNMISC1], (instregex "^F(AC|CM)(EQ|GE|GT|LE|LT)v[^1]")>;
def : InstRW<[M1WriteFCVT3],  (instregex "^[FVSU]CVTX?[AFLMNPZ][SU]?(_Int)?v")>;
def : InstRW<[M1WriteFVAR15], (instregex "FDIVv.f32")>;
def : InstRW<[M1WriteFVAR23], (instregex "FDIVv2f64")>;
def : InstRW<[M1WriteFVAR15], (instregex "FSQRTv.f32")>;
def : InstRW<[M1WriteFVAR23], (instregex "FSQRTv2f64")>;
def : InstRW<[M1WriteNMISC1], (instregex "^F(MAX|MIN)(NM)?V?v")>;
def : InstRW<[M1WriteNMISC2], (instregex "^F(MAX|MIN)(NM)?Pv")>;
def : InstRW<[M1WriteNEONJ],  (instregex "^FMULX?v.i")>;
def : InstRW<[M1WriteFMAC4],  (instregex "^FMULX?v.f")>;
def : InstRW<[M1WriteNEONK],  (instregex "^FML[AS]v.i")>;
def : InstRW<[M1WriteFMAC5],  (instregex "^FML[AS]v.f")>;
def : InstRW<[M1WriteFCVT3],  (instregex "^FRINT[AIMNPXZ]v")>;

// ASIMD miscellaneous instructions.
def : InstRW<[M1WriteNALU1],  (instregex "^RBITv")>;
def : InstRW<[M1WriteNAL11],  (instregex "^(BIF|BIT|BSL)v")>;
def : InstRW<[M1WriteNEONB],  (instregex "^DUPv.+gpr")>;
def : InstRW<[M1WriteNALU1],  (instregex "^DUPv.+lane")>;
def : InstRW<[M1WriteNALU1],  (instregex "^EXTv8")>;
def : InstRW<[M1WriteNEONL],  (instregex "^EXTv16")>;
def : InstRW<[M1WriteNAL13],  (instregex "^[SU]?Q?XTU?Nv")>;
def : InstRW<[M1WriteNALU1],  (instregex "^CPY")>;
def : InstRW<[M1WriteNALU1],  (instregex "^INSv.+lane")>;
def : InstRW<[M1WriteNALU1],  (instregex "^MOVI[Dv]")>;
def : InstRW<[M1WriteNALU1],  (instregex "^FMOVv")>;
def : InstRW<[M1WriteFCVT4],  (instregex "^[FU](RECP|RSQRT)Ev[248]")>;
def : InstRW<[M1WriteFMAC5],  (instregex "^F(RECP|RSQRT)Sv")>;
def : InstRW<[M1WriteNALU1],  (instregex "^REV(16|32|64)v")>;
def : InstRW<[M1WriteNAL11],  (instregex "^TB[LX]v8i8One")>;
def : InstRW<[WriteSequence<[M1WriteNAL11], 2>],
                              (instregex "^TB[LX]v8i8Two")>;
def : InstRW<[WriteSequence<[M1WriteNAL11], 3>],
                              (instregex "^TB[LX]v8i8Three")>;
def : InstRW<[WriteSequence<[M1WriteNAL11], 4>],
                              (instregex "^TB[LX]v8i8Four")>;
def : InstRW<[M1WriteNAL12],  (instregex "^TB[LX]v16i8One")>;
def : InstRW<[WriteSequence<[M1WriteNAL12], 2>],
                              (instregex "^TB[LX]v16i8Two")>;
def : InstRW<[WriteSequence<[M1WriteNAL12], 3>],
                              (instregex "^TB[LX]v16i8Three")>;
def : InstRW<[WriteSequence<[M1WriteNAL12], 4>],
                              (instregex "^TB[LX]v16i8Four")>;
def : InstRW<[M1WriteNEOND],  (instregex "^[SU]MOVv")>;
def : InstRW<[M1WriteNEONC],  (instregex "^INSv.+gpr")>;
def : InstRW<[M1WriteNALU1],  (instregex "^(TRN|UZP)[12](v8i8|v4i16|v2i32)")>;
def : InstRW<[M1WriteNALU2],  (instregex "^(TRN|UZP)[12](v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[M1WriteNALU1],  (instregex "^ZIP[12]v")>;

// ASIMD load instructions.
def : InstRW<[M1WriteVLDD],   (instregex "LD1i(8|16|32)$")>;
def : InstRW<[M1WriteVLDD,
              WriteAdr],      (instregex "LD1i(8|16|32)_POST$")>;
def : InstRW<[M1WriteVLDE],   (instregex "LD1i(64)$")>;
def : InstRW<[M1WriteVLDE,
              WriteAdr],      (instregex "LD1i(64)_POST$")>;

def : InstRW<[WriteVLD],      (instregex "LD1Rv(8b|4h|2s)$")>;
def : InstRW<[WriteVLD,
              WriteAdr],      (instregex "LD1Rv(8b|4h|2s)_POST$")>;
def : InstRW<[WriteVLD],      (instregex "LD1Rv(1d)$")>;
def : InstRW<[WriteVLD,
              WriteAdr],      (instregex "LD1Rv(1d)_POST$")>;
def : InstRW<[WriteVLD],      (instregex "LD1Rv(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLD,
              WriteAdr],      (instregex "LD1Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[WriteVLD],      (instregex "LD1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVLD,
              WriteAdr],      (instregex "LD1Onev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteVLD],      (instregex "LD1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVLD,
              WriteAdr],      (instregex "LD1Onev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M1WriteVLDA],   (instregex "LD1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[M1WriteVLDA,
              WriteAdr],      (instregex "LD1Twov(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M1WriteVLDA],   (instregex "LD1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M1WriteVLDA,
              WriteAdr],      (instregex "LD1Twov(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M1WriteVLDB],   (instregex "LD1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[M1WriteVLDB,
              WriteAdr],      (instregex "LD1Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M1WriteVLDB],   (instregex "LD1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M1WriteVLDB,
              WriteAdr],      (instregex "LD1Threev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M1WriteVLDC],   (instregex "LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[M1WriteVLDC,
              WriteAdr],      (instregex "LD1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M1WriteVLDC],   (instregex "LD1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M1WriteVLDC,
              WriteAdr],      (instregex "LD1Fourv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M1WriteVLDG],   (instregex "LD2i(8|16)$")>;
def : InstRW<[M1WriteVLDG,
              WriteAdr],      (instregex "LD2i(8|16)_POST$")>;
def : InstRW<[M1WriteVLDG],   (instregex "LD2i(32)$")>;
def : InstRW<[M1WriteVLDG,
              WriteAdr],      (instregex "LD2i(32)_POST$")>;
def : InstRW<[M1WriteVLDH],   (instregex "LD2i(64)$")>;
def : InstRW<[M1WriteVLDH,
              WriteAdr],      (instregex "LD2i(64)_POST$")>;

def : InstRW<[M1WriteVLDA],   (instregex "LD2Rv(8b|4h|2s)$")>;
def : InstRW<[M1WriteVLDA,
              WriteAdr],      (instregex "LD2Rv(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVLDA],   (instregex "LD2Rv(1d)$")>;
def : InstRW<[M1WriteVLDA,
              WriteAdr],      (instregex "LD2Rv(1d)_POST$")>;
def : InstRW<[M1WriteVLDA],   (instregex "LD2Rv(16b|8h|4s|2d)$")>;
def : InstRW<[M1WriteVLDA,
              WriteAdr],      (instregex "LD2Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M1WriteVLDF],   (instregex "LD2Twov(8b|4h|2s)$")>;
def : InstRW<[M1WriteVLDF,
              WriteAdr],      (instregex "LD2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVLDF],   (instregex "LD2Twov(16b|8h|4s)$")>;
def : InstRW<[M1WriteVLDF,
              WriteAdr],      (instregex "LD2Twov(16b|8h|4s)_POST$")>;
def : InstRW<[M1WriteVLDF],   (instregex "LD2Twov(2d)$")>;
def : InstRW<[M1WriteVLDF,
              WriteAdr],      (instregex "LD2Twov(2d)_POST$")>;

def : InstRW<[M1WriteVLDJ],   (instregex "LD3i(8|16)$")>;
def : InstRW<[M1WriteVLDJ,
              WriteAdr],      (instregex "LD3i(8|16)_POST$")>;
def : InstRW<[M1WriteVLDJ],   (instregex "LD3i(32)$")>;
def : InstRW<[M1WriteVLDJ,
              WriteAdr],      (instregex "LD3i(32)_POST$")>;
def : InstRW<[M1WriteVLDL],   (instregex "LD3i(64)$")>;
def : InstRW<[M1WriteVLDL,
              WriteAdr],      (instregex "LD3i(64)_POST$")>;

def : InstRW<[M1WriteVLDB],   (instregex "LD3Rv(8b|4h|2s)$")>;
def : InstRW<[M1WriteVLDB,
              WriteAdr],      (instregex "LD3Rv(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVLDB],   (instregex "LD3Rv(1d)$")>;
def : InstRW<[M1WriteVLDB,
              WriteAdr],      (instregex "LD3Rv(1d)_POST$")>;
def : InstRW<[M1WriteVLDB],   (instregex "LD3Rv(16b|8h|4s)$")>;
def : InstRW<[M1WriteVLDB,
              WriteAdr],      (instregex "LD3Rv(16b|8h|4s)_POST$")>;
def : InstRW<[M1WriteVLDB],   (instregex "LD3Rv(2d)$")>;
def : InstRW<[M1WriteVLDB,
              WriteAdr],      (instregex "LD3Rv(2d)_POST$")>;

def : InstRW<[M1WriteVLDI],   (instregex "LD3Threev(8b|4h|2s)$")>;
def : InstRW<[M1WriteVLDI,
              WriteAdr],      (instregex "LD3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVLDI],   (instregex "LD3Threev(16b|8h|4s)$")>;
def : InstRW<[M1WriteVLDI,
              WriteAdr],      (instregex "LD3Threev(16b|8h|4s)_POST$")>;
def : InstRW<[M1WriteVLDI],   (instregex "LD3Threev(2d)$")>;
def : InstRW<[M1WriteVLDI,
              WriteAdr],      (instregex "LD3Threev(2d)_POST$")>;

def : InstRW<[M1WriteVLDK],   (instregex "LD4i(8|16)$")>;
def : InstRW<[M1WriteVLDK,
              WriteAdr],      (instregex "LD4i(8|16)_POST$")>;
def : InstRW<[M1WriteVLDK],   (instregex "LD4i(32)$")>;
def : InstRW<[M1WriteVLDK,
              WriteAdr],      (instregex "LD4i(32)_POST$")>;
def : InstRW<[M1WriteVLDM],   (instregex "LD4i(64)$")>;
def : InstRW<[M1WriteVLDM,
              WriteAdr],      (instregex "LD4i(64)_POST$")>;

def : InstRW<[M1WriteVLDC],   (instregex "LD4Rv(8b|4h|2s)$")>;
def : InstRW<[M1WriteVLDC,
              WriteAdr],      (instregex "LD4Rv(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVLDC],   (instregex "LD4Rv(1d)$")>;
def : InstRW<[M1WriteVLDC,
              WriteAdr],      (instregex "LD4Rv(1d)_POST$")>;
def : InstRW<[M1WriteVLDC],   (instregex "LD4Rv(16b|8h|4s)$")>;
def : InstRW<[M1WriteVLDC,
              WriteAdr],      (instregex "LD4Rv(16b|8h|4s)_POST$")>;
def : InstRW<[M1WriteVLDC],   (instregex "LD4Rv(2d)$")>;
def : InstRW<[M1WriteVLDC,
              WriteAdr],      (instregex "LD4Rv(2d)_POST$")>;

def : InstRW<[M1WriteVLDN],   (instregex "LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[M1WriteVLDN,
              WriteAdr],      (instregex "LD4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVLDN],   (instregex "LD4Fourv(16b|8h|4s)$")>;
def : InstRW<[M1WriteVLDN,
              WriteAdr],      (instregex "LD4Fourv(16b|8h|4s)_POST$")>;
def : InstRW<[M1WriteVLDN],   (instregex "LD4Fourv(2d)$")>;
def : InstRW<[M1WriteVLDN,
              WriteAdr],      (instregex "LD4Fourv(2d)_POST$")>;

// ASIMD store instructions.
def : InstRW<[M1WriteVSTD],   (instregex "ST1i(8|16|32)$")>;
def : InstRW<[M1WriteVSTD,
              WriteAdr],      (instregex "ST1i(8|16|32)_POST$")>;
def : InstRW<[M1WriteVSTD],   (instregex "ST1i(64)$")>;
def : InstRW<[M1WriteVSTD,
              WriteAdr],      (instregex "ST1i(64)_POST$")>;

def : InstRW<[WriteVST],      (instregex "ST1Onev(8b|4h|2s|1d)$")>;
def : InstRW<[WriteVST,
              WriteAdr],      (instregex "ST1Onev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[WriteVST],      (instregex "ST1Onev(16b|8h|4s|2d)$")>;
def : InstRW<[WriteVST,
              WriteAdr],      (instregex "ST1Onev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M1WriteVSTA],   (instregex "ST1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[M1WriteVSTA,
              WriteAdr],      (instregex "ST1Twov(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M1WriteVSTA],   (instregex "ST1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[M1WriteVSTA,
              WriteAdr],      (instregex "ST1Twov(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M1WriteVSTB],   (instregex "ST1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[M1WriteVSTB,
              WriteAdr],      (instregex "ST1Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M1WriteVSTB],   (instregex "ST1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[M1WriteVSTB,
              WriteAdr],      (instregex "ST1Threev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[M1WriteVSTC],   (instregex "ST1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[M1WriteVSTC,
              WriteAdr],      (instregex "ST1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[M1WriteVSTC],   (instregex "ST1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[M1WriteVSTC,
              WriteAdr], (instregex "ST1Fourv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[M1WriteVSTD],   (instregex "ST2i(8|16|32)$")>;
def : InstRW<[M1WriteVSTD,
              WriteAdr],      (instregex "ST2i(8|16|32)_POST$")>;
def : InstRW<[M1WriteVSTD],   (instregex "ST2i(64)$")>;
def : InstRW<[M1WriteVSTD,
              WriteAdr],      (instregex "ST2i(64)_POST$")>;

def : InstRW<[M1WriteVSTD],   (instregex "ST2Twov(8b|4h|2s)$")>;
def : InstRW<[M1WriteVSTD,
              WriteAdr],      (instregex "ST2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVSTE],   (instregex "ST2Twov(16b|8h|4s)$")>;
def : InstRW<[M1WriteVSTE,
              WriteAdr],      (instregex "ST2Twov(16b|8h|4s)_POST$")>;
def : InstRW<[M1WriteVSTE],   (instregex "ST2Twov(2d)$")>;
def : InstRW<[M1WriteVSTE,
              WriteAdr],      (instregex "ST2Twov(2d)_POST$")>;

def : InstRW<[M1WriteVSTH],   (instregex "ST3i(8|16)$")>;
def : InstRW<[M1WriteVSTH,
              WriteAdr],      (instregex "ST3i(8|16)_POST$")>;
def : InstRW<[M1WriteVSTH],   (instregex "ST3i(32)$")>;
def : InstRW<[M1WriteVSTH,
              WriteAdr],      (instregex "ST3i(32)_POST$")>;
def : InstRW<[M1WriteVSTF],   (instregex "ST3i(64)$")>;
def : InstRW<[M1WriteVSTF,
              WriteAdr],      (instregex "ST3i(64)_POST$")>;

def : InstRW<[M1WriteVSTF],   (instregex "ST3Threev(8b|4h|2s)$")>;
def : InstRW<[M1WriteVSTF,
              WriteAdr],      (instregex "ST3Threev(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVSTG],   (instregex "ST3Threev(16b|8h|4s)$")>;
def : InstRW<[M1WriteVSTG,
              WriteAdr],      (instregex "ST3Threev(16b|8h|4s)_POST$")>;
def : InstRW<[M1WriteVSTG],   (instregex "ST3Threev(2d)$")>;
def : InstRW<[M1WriteVSTG,
              WriteAdr],      (instregex "ST3Threev(2d)_POST$")>;

def : InstRW<[M1WriteVSTH],   (instregex "ST4i(8|16)$")>;
def : InstRW<[M1WriteVSTH,
              WriteAdr],      (instregex "ST4i(8|16)_POST$")>;
def : InstRW<[M1WriteVSTH],   (instregex "ST4i(32)$")>;
def : InstRW<[M1WriteVSTH,
              WriteAdr],      (instregex "ST4i(32)_POST$")>;
def : InstRW<[M1WriteVSTF],   (instregex "ST4i(64)$")>;
def : InstRW<[M1WriteVSTF,
              WriteAdr],      (instregex "ST4i(64)_POST$")>;

def : InstRW<[M1WriteVSTF],   (instregex "ST4Fourv(8b|4h|2s)$")>;
def : InstRW<[M1WriteVSTF,
              WriteAdr],      (instregex "ST4Fourv(8b|4h|2s)_POST$")>;
def : InstRW<[M1WriteVSTI],   (instregex "ST4Fourv(16b|8h|4s)$")>;
def : InstRW<[M1WriteVSTI,
              WriteAdr],      (instregex "ST4Fourv(16b|8h|4s)_POST$")>;
def : InstRW<[M1WriteVSTI],   (instregex "ST4Fourv(2d)$")>;
def : InstRW<[M1WriteVSTI,
              WriteAdr],      (instregex "ST4Fourv(2d)_POST$")>;

// Cryptography instructions.
def : InstRW<[M1WriteAES], (instregex "^AES[DE]")>;
def : InstRW<[M1WriteAES, M1ReadAES], (instregex "^AESI?MC")>;

def : InstRW<[M1WriteNCRYPT1], (instregex "^PMUL")>;
def : InstRW<[M1WriteNCRYPT1], (instregex "^SHA1(H|SU)")>;
def : InstRW<[M1WriteNCRYPT5], (instregex "^SHA1[CMP]")>;
def : InstRW<[M1WriteNCRYPT1], (instregex "^SHA256SU0")>;
def : InstRW<[M1WriteNCRYPT5], (instregex "^SHA256(H|SU1)")>;

// CRC instructions.
def : InstRW<[M1WriteC2], (instregex "^CRC32")>;

} // SchedModel = ExynosM1Model
