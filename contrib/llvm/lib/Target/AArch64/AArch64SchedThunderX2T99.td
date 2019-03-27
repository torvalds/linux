//=- AArch64SchedThunderX2T99.td - Cavium ThunderX T99 ---*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the scheduling model for Cavium ThunderX2T99
// processors.
// Based on Broadcom Vulcan.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// 2. Pipeline Description.

def ThunderX2T99Model : SchedMachineModel {
  let IssueWidth            =   4; // 4 micro-ops dispatched at a time.
  let MicroOpBufferSize     = 180; // 180 entries in micro-op re-order buffer.
  let LoadLatency           =   4; // Optimistic load latency.
  let MispredictPenalty     =  12; // Extra cycles for mispredicted branch.
  // Determined via a mix of micro-arch details and experimentation.
  let LoopMicroOpBufferSize = 128;
  let PostRAScheduler       =   1; // Using PostRA sched.
  let CompleteModel         =   1;

  list<Predicate> UnsupportedFeatures = [HasSVE];

  // FIXME: Remove when all errors have been fixed.
  let FullInstRWOverlapCheck = 0;
}

let SchedModel = ThunderX2T99Model in {

// Define the issue ports.

// Port 0: ALU, FP/SIMD.
def THX2T99P0 : ProcResource<1>;

// Port 1: ALU, FP/SIMD, integer mul/div.
def THX2T99P1 : ProcResource<1>;

// Port 2: ALU, Branch.
def THX2T99P2 : ProcResource<1>;

// Port 3: Store data.
def THX2T99P3 : ProcResource<1>;

// Port 4: Load/store.
def THX2T99P4 : ProcResource<1>;

// Port 5: Load/store.
def THX2T99P5 : ProcResource<1>;

// Define groups for the functional units on each issue port.  Each group
// created will be used by a WriteRes later on.
//
// NOTE: Some groups only contain one member.  This is a way to create names for
// the various functional units that share a single issue port.  For example,
// "THX2T99I1" for ALU ops on port 1 and "THX2T99F1" for FP ops on port 1.

// Integer divide and multiply micro-ops only on port 1.
def THX2T99I1 : ProcResGroup<[THX2T99P1]>;

// Branch micro-ops only on port 2.
def THX2T99I2 : ProcResGroup<[THX2T99P2]>;

// ALU micro-ops on ports 0, 1, and 2.
def THX2T99I012 : ProcResGroup<[THX2T99P0, THX2T99P1, THX2T99P2]>;

// Crypto FP/SIMD micro-ops only on port 1.
def THX2T99F1 : ProcResGroup<[THX2T99P1]>;

// FP/SIMD micro-ops on ports 0 and 1.
def THX2T99F01 : ProcResGroup<[THX2T99P0, THX2T99P1]>;

// Store data micro-ops only on port 3.
def THX2T99SD : ProcResGroup<[THX2T99P3]>;

// Load/store micro-ops on ports 4 and 5.
def THX2T99LS01 : ProcResGroup<[THX2T99P4, THX2T99P5]>;

// 60 entry unified scheduler.
def THX2T99Any : ProcResGroup<[THX2T99P0, THX2T99P1, THX2T99P2,
                               THX2T99P3, THX2T99P4, THX2T99P5]> {
  let BufferSize = 60;
}

// Define commonly used write types for InstRW specializations.
// All definitions follow the format: THX2T99Write_<NumCycles>Cyc_<Resources>.

// 3 cycles on I1.
def THX2T99Write_3Cyc_I1 : SchedWriteRes<[THX2T99I1]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

// 1 cycles on I2.
def THX2T99Write_1Cyc_I2 : SchedWriteRes<[THX2T99I2]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 4 cycles on I1.
def THX2T99Write_4Cyc_I1 : SchedWriteRes<[THX2T99I1]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

// 23 cycles on I1.
def THX2T99Write_23Cyc_I1 : SchedWriteRes<[THX2T99I1]> {
  let Latency = 23;
  let ResourceCycles = [13, 23];
  let NumMicroOps = 4;
}

// 39 cycles on I1.
def THX2T99Write_39Cyc_I1 : SchedWriteRes<[THX2T99I1]> {
  let Latency = 39;
  let ResourceCycles = [13, 39];
  let NumMicroOps = 4;
}

// 1 cycle on I0, I1, or I2.
def THX2T99Write_1Cyc_I012 : SchedWriteRes<[THX2T99I012]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 2 cycles on I0, I1, or I2.
def THX2T99Write_2Cyc_I012 : SchedWriteRes<[THX2T99I012]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

// 4 cycles on I0, I1, or I2.
def THX2T99Write_4Cyc_I012 : SchedWriteRes<[THX2T99I012]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

// 5 cycles on I0, I1, or I2.
def THX2T99Write_5Cyc_I012 : SchedWriteRes<[THX2T99I012]> {
  let Latency = 2;
  let NumMicroOps = 3;
}

// 5 cycles on F1.
def THX2T99Write_5Cyc_F1 : SchedWriteRes<[THX2T99F1]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// 7 cycles on F1.
def THX2T99Write_7Cyc_F1 : SchedWriteRes<[THX2T99F1]> {
  let Latency = 7;
  let NumMicroOps = 2;
}

// 4 cycles on F0 or F1.
def THX2T99Write_4Cyc_F01 : SchedWriteRes<[THX2T99F01]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

// 5 cycles on F0 or F1.
def THX2T99Write_5Cyc_F01 : SchedWriteRes<[THX2T99F01]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// 6 cycles on F0 or F1.
def THX2T99Write_6Cyc_F01 : SchedWriteRes<[THX2T99F01]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 7 cycles on F0 or F1.
def THX2T99Write_7Cyc_F01 : SchedWriteRes<[THX2T99F01]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

// 8 cycles on F0 or F1.
def THX2T99Write_8Cyc_F01 : SchedWriteRes<[THX2T99F01]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

// 10 cycles on F0 or F1.
def THX2T99Write_10Cyc_F01 : SchedWriteRes<[THX2T99F01]> {
  let Latency = 10;
  let NumMicroOps = 3;
}

// 16 cycles on F0 or F1.
def THX2T99Write_16Cyc_F01 : SchedWriteRes<[THX2T99F01]> {
  let Latency = 16;
  let NumMicroOps = 3;
  let ResourceCycles = [8];
}

// 23 cycles on F0 or F1.
def THX2T99Write_23Cyc_F01 : SchedWriteRes<[THX2T99F01]> {
  let Latency = 23;
  let NumMicroOps = 3;
  let ResourceCycles = [11];
}

// 1 cycles on LS0 or LS1.
def THX2T99Write_1Cyc_LS01 : SchedWriteRes<[THX2T99LS01]> {
  let Latency = 0;
}

// 1 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_1Cyc_LS01_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 0;
  let NumMicroOps = 2;
}

// 1 cycles on LS0 or LS1 and 2 of I0, I1, or I2.
def THX2T99Write_1Cyc_LS01_I012_I012 :
  SchedWriteRes<[THX2T99LS01, THX2T99I012, THX2T99I012]> {
  let Latency = 0;
  let NumMicroOps = 3;
}

// 2 cycles on LS0 or LS1.
def THX2T99Write_2Cyc_LS01 : SchedWriteRes<[THX2T99LS01]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 4 cycles on LS0 or LS1.
def THX2T99Write_4Cyc_LS01 : SchedWriteRes<[THX2T99LS01]> {
  let Latency = 4;
  let NumMicroOps = 4;
}

// 5 cycles on LS0 or LS1.
def THX2T99Write_5Cyc_LS01 : SchedWriteRes<[THX2T99LS01]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 6 cycles on LS0 or LS1.
def THX2T99Write_6Cyc_LS01 : SchedWriteRes<[THX2T99LS01]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 4 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_4Cyc_LS01_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

// 4 cycles on LS0 or LS1 and 2 of I0, I1, or I2.
def THX2T99Write_4Cyc_LS01_I012_I012 :
  SchedWriteRes<[THX2T99LS01, THX2T99I012, THX2T99I012]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

// 5 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_5Cyc_LS01_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 5 cycles on LS0 or LS1 and 2 of I0, I1, or I2.
def THX2T99Write_5Cyc_LS01_I012_I012 :
  SchedWriteRes<[THX2T99LS01, THX2T99I012, THX2T99I012]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 6 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_6Cyc_LS01_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 6;
  let NumMicroOps = 4;
}

// 6 cycles on LS0 or LS1 and 2 of I0, I1, or I2.
def THX2T99Write_6Cyc_LS01_I012_I012 :
  SchedWriteRes<[THX2T99LS01, THX2T99I012, THX2T99I012]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 1 cycles on LS0 or LS1 and F0 or F1.
def THX2T99Write_1Cyc_LS01_F01 : SchedWriteRes<[THX2T99LS01, THX2T99F01]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// 5 cycles on LS0 or LS1 and F0 or F1.
def THX2T99Write_5Cyc_LS01_F01 : SchedWriteRes<[THX2T99LS01, THX2T99F01]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// 6 cycles on LS0 or LS1 and F0 or F1.
def THX2T99Write_6Cyc_LS01_F01 : SchedWriteRes<[THX2T99LS01, THX2T99F01]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

// 7 cycles on LS0 or LS1 and F0 or F1.
def THX2T99Write_7Cyc_LS01_F01 : SchedWriteRes<[THX2T99LS01, THX2T99F01]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

// 8 cycles on LS0 or LS1 and F0 or F1.
def THX2T99Write_8Cyc_LS01_F01 : SchedWriteRes<[THX2T99LS01, THX2T99F01]> {
  let Latency = 8;
  let NumMicroOps = 3;
}

// 8 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_8Cyc_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 8;
  let NumMicroOps = 4;
}

// 12 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_12Cyc_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 12;
  let NumMicroOps = 6;
}

// 16 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_16Cyc_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 16;
  let NumMicroOps = 8;
}

// 24 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_24Cyc_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 24;
  let NumMicroOps = 12;
}

// 32 cycles on LS0 or LS1 and I0, I1, or I2.
def THX2T99Write_32Cyc_I012 : SchedWriteRes<[THX2T99LS01, THX2T99I012]> {
  let Latency = 32;
  let NumMicroOps = 16;
}

// Define commonly used read types.

// No forwarding is provided for these types.
def : ReadAdvance<ReadI,       0>;
def : ReadAdvance<ReadISReg,   0>;
def : ReadAdvance<ReadIEReg,   0>;
def : ReadAdvance<ReadIM,      0>;
def : ReadAdvance<ReadIMA,     0>;
def : ReadAdvance<ReadID,      0>;
def : ReadAdvance<ReadExtrHi,  0>;
def : ReadAdvance<ReadAdrBase, 0>;
def : ReadAdvance<ReadVLD,     0>;

//===----------------------------------------------------------------------===//
// 3. Instruction Tables.

//---
// 3.1 Branch Instructions
//---

// Branch, immed
// Branch and link, immed
// Compare and branch
def : WriteRes<WriteBr,      [THX2T99I2]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Branch, register
// Branch and link, register != LR
// Branch and link, register = LR
def : WriteRes<WriteBrReg,   [THX2T99I2]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def : WriteRes<WriteSys,     []> { let Latency = 1; }
def : WriteRes<WriteBarrier, []> { let Latency = 1; }
def : WriteRes<WriteHint,    []> { let Latency = 1; }

def : WriteRes<WriteAtomic,  []> {
  let Latency = 4;
  let NumMicroOps = 2;
}

//---
// Branch
//---
def : InstRW<[THX2T99Write_1Cyc_I2], (instrs B, BL, BR, BLR)>;
def : InstRW<[THX2T99Write_1Cyc_I2], (instrs RET)>;
def : InstRW<[THX2T99Write_1Cyc_I2], (instregex "^B..$")>;
def : InstRW<[THX2T99Write_1Cyc_I2],
            (instregex "^CBZ", "^CBNZ", "^TBZ", "^TBNZ")>;

//---
// 3.2 Arithmetic and Logical Instructions
// 3.3 Move and Shift Instructions
//---


// ALU, basic
// Conditional compare
// Conditional select
// Address generation
def : WriteRes<WriteI,       [THX2T99I012]> {
  let Latency = 1;
  let ResourceCycles = [1];
  let NumMicroOps = 2;
}

def : InstRW<[WriteI],
            (instregex "ADD?(W|X)r(i|r|s|x)",   "ADDS?(W|X)r(i|r|s|x)(64)?",
                       "AND?(W|X)r(i|r|s|x)",   "ANDS?(W|X)r(i|r|s|x)",
                       "ADC(W|X)r",
                       "BIC?(W|X)r(i|r|s|x)",   "BICS?(W|X)r(i|r|s|x)",
                       "EON?(W|X)r(i|r|s|x)",   "ORN?(W|X)r(i|r|s|x)",
                       "ORR?(W|X)r(i|r|s|x)",   "SUB?(W|X)r(i|r|s|x)",
                       "SUBS?(W|X)r(i|r|s|x)",  "SBC(W|X)r",
                       "SBCS(W|X)r",            "CCMN(W|X)(i|r)",
                       "CCMP(W|X)(i|r)",        "CSEL(W|X)r",
                       "CSINC(W|X)r",           "CSINV(W|X)r",
                       "CSNEG(W|X)r")>;

def : InstRW<[WriteI], (instrs COPY)>;

// ALU, extend and/or shift
def : WriteRes<WriteISReg,   [THX2T99I012]> {
  let Latency = 2;
  let ResourceCycles = [2];
  let NumMicroOps = 2;
}

def : InstRW<[WriteISReg],
            (instregex "ADD?(W|X)r(i|r|s|x)",   "ADDS?(W|X)r(i|r|s|x)(64)?",
                       "AND?(W|X)r(i|r|s|x)",   "ANDS?(W|X)r(i|r|s|x)",
                       "ADC(W|X)r",
                       "BIC?(W|X)r(i|r|s|x)",   "BICS?(W|X)r(i|r|s|x)",
                       "EON?(W|X)r(i|r|s|x)",   "ORN?(W|X)r(i|r|s|x)",
                       "ORR?(W|X)r(i|r|s|x)",   "SUB?(W|X)r(i|r|s|x)",
                       "SUBS?(W|X)r(i|r|s|x)",  "SBC(W|X)r",
                       "SBCS(W|X)r",            "CCMN(W|X)(i|r)",
                       "CCMP(W|X)(i|r)",        "CSEL(W|X)r",
                       "CSINC(W|X)r",           "CSINV(W|X)r",
                       "CSNEG(W|X)r")>;

def : WriteRes<WriteIEReg,   [THX2T99I012]> {
  let Latency = 1;
  let ResourceCycles = [1];
  let NumMicroOps = 2;
}

def : InstRW<[WriteIEReg],
            (instregex "ADD?(W|X)r(i|r|s|x)",   "ADDS?(W|X)r(i|r|s|x)(64)?",
                       "AND?(W|X)r(i|r|s|x)",   "ANDS?(W|X)r(i|r|s|x)",
                       "ADC(W|X)r",
                       "BIC?(W|X)r(i|r|s|x)",   "BICS?(W|X)r(i|r|s|x)",
                       "EON?(W|X)r(i|r|s|x)",   "ORN?(W|X)r(i|r|s|x)",
                       "ORR?(W|X)r(i|r|s|x)",   "SUB?(W|X)r(i|r|s|x)",
                       "SUBS?(W|X)r(i|r|s|x)",  "SBC(W|X)r",
                       "SBCS(W|X)r",            "CCMN(W|X)(i|r)",
                       "CCMP(W|X)(i|r)",        "CSEL(W|X)r",
                       "CSINC(W|X)r",           "CSINV(W|X)r",
                       "CSNEG(W|X)r")>;

// Move immed
def : WriteRes<WriteImm,     [THX2T99I012]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def : InstRW<[THX2T99Write_1Cyc_I012],
            (instrs MOVKWi, MOVKXi, MOVNWi, MOVNXi, MOVZWi, MOVZXi)>;

def : InstRW<[THX2T99Write_1Cyc_I012],
            (instrs ASRVWr, ASRVXr, LSLVWr, LSLVXr, RORVWr, RORVXr)>;

// Variable shift
def : WriteRes<WriteIS,      [THX2T99I012]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

//---
// 3.4 Divide and Multiply Instructions
//---

// Divide, W-form
// Latency range of 13-23/13-39.
def : WriteRes<WriteID32,    [THX2T99I1]> {
  let Latency = 39;
  let ResourceCycles = [39];
  let NumMicroOps = 4;
}

// Divide, X-form
def : WriteRes<WriteID64,    [THX2T99I1]> {
  let Latency = 23;
  let ResourceCycles = [23];
  let NumMicroOps = 4;
}

// Multiply accumulate, W-form
def : WriteRes<WriteIM32,    [THX2T99I012]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

// Multiply accumulate, X-form
def : WriteRes<WriteIM64,    [THX2T99I012]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

//def : InstRW<[WriteIM32, ReadIM, ReadIM, ReadIMA, THX2T99Write_5Cyc_I012],
//             (instrs MADDWrrr, MSUBWrrr)>;
def : InstRW<[WriteIM32], (instrs MADDWrrr, MSUBWrrr)>;
def : InstRW<[WriteIM32], (instrs MADDXrrr, MSUBXrrr)>;
def : InstRW<[THX2T99Write_5Cyc_I012],
            (instregex "(S|U)(MADDL|MSUBL)rrr")>;

def : InstRW<[WriteID32], (instrs SDIVWr, UDIVWr)>;
def : InstRW<[WriteID64], (instrs SDIVXr, UDIVXr)>;

// Bitfield extract, two reg
def : WriteRes<WriteExtr,    [THX2T99I012]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Multiply high
def : InstRW<[THX2T99Write_4Cyc_I1], (instrs SMULHrr, UMULHrr)>;

// Miscellaneous Data-Processing Instructions
// Bitfield extract
def : InstRW<[THX2T99Write_1Cyc_I012], (instrs EXTRWrri, EXTRXrri)>;

// Bitifield move - basic
def : InstRW<[THX2T99Write_1Cyc_I012],
            (instrs SBFMWri, SBFMXri, UBFMWri, UBFMXri)>;

// Bitfield move, insert
def : InstRW<[THX2T99Write_1Cyc_I012], (instregex "^BFM")>;
def : InstRW<[THX2T99Write_1Cyc_I012], (instregex "(S|U)?BFM.*")>;

// Count leading
def : InstRW<[THX2T99Write_3Cyc_I1], (instregex "^CLS(W|X)r$",
                                                "^CLZ(W|X)r$")>;

// Reverse bits
def : InstRW<[THX2T99Write_1Cyc_I012], (instrs RBITWr, RBITXr)>;

// Cryptography Extensions
def : InstRW<[THX2T99Write_5Cyc_F1], (instregex "^AES[DE]")>;
def : InstRW<[THX2T99Write_5Cyc_F1], (instregex "^AESI?MC")>;
def : InstRW<[THX2T99Write_5Cyc_F1], (instregex "^PMULL")>;
def : InstRW<[THX2T99Write_7Cyc_F1], (instregex "^SHA1SU0")>;
def : InstRW<[THX2T99Write_7Cyc_F1], (instregex "^SHA1(H|SU1)")>;
def : InstRW<[THX2T99Write_7Cyc_F1], (instregex "^SHA1[CMP]")>;
def : InstRW<[THX2T99Write_7Cyc_F1], (instregex "^SHA256SU0")>;
def : InstRW<[THX2T99Write_7Cyc_F1], (instregex "^SHA256(H|H2|SU1)")>;

// CRC Instructions
// def : InstRW<[THX2T99Write_4Cyc_I1], (instregex "^CRC32", "^CRC32C")>;
def : InstRW<[THX2T99Write_4Cyc_I1],
            (instrs CRC32Brr, CRC32Hrr, CRC32Wrr, CRC32Xrr)>;

def : InstRW<[THX2T99Write_4Cyc_I1],
            (instrs CRC32CBrr, CRC32CHrr, CRC32CWrr, CRC32CXrr)>;

// Reverse bits/bytes
// NOTE: Handled by WriteI.

//---
// 3.6 Load Instructions
// 3.10 FP Load Instructions
//---

// Load register, literal
// Load register, unscaled immed
// Load register, immed unprivileged
// Load register, unsigned immed
def : WriteRes<WriteLD,      [THX2T99LS01]> {
  let Latency = 4;
  let NumMicroOps = 4;
}

// Load register, immed post-index
// NOTE: Handled by WriteLD, WriteI.
// Load register, immed pre-index
// NOTE: Handled by WriteLD, WriteAdr.
def : WriteRes<WriteAdr,     [THX2T99I012]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Load pair, immed offset, normal
// Load pair, immed offset, signed words, base != SP
// Load pair, immed offset signed words, base = SP
// LDP only breaks into *one* LS micro-op.  Thus
// the resources are handled by WriteLD.
def : WriteRes<WriteLDHi,    []> {
  let Latency = 5;
  let NumMicroOps = 5;
}

// Load register offset, basic
// Load register, register offset, scale by 4/8
// Load register, register offset, scale by 2
// Load register offset, extend
// Load register, register offset, extend, scale by 4/8
// Load register, register offset, extend, scale by 2
def THX2T99WriteLDIdx : SchedWriteVariant<[
  SchedVar<ScaledIdxPred, [THX2T99Write_6Cyc_LS01_I012_I012]>,
  SchedVar<NoSchedPred,   [THX2T99Write_5Cyc_LS01_I012]>]>;
def : SchedAlias<WriteLDIdx, THX2T99WriteLDIdx>;

def THX2T99ReadAdrBase : SchedReadVariant<[
  SchedVar<ScaledIdxPred, [ReadDefault]>,
  SchedVar<NoSchedPred,   [ReadDefault]>]>;
def : SchedAlias<ReadAdrBase, THX2T99ReadAdrBase>;

// Load pair, immed pre-index, normal
// Load pair, immed pre-index, signed words
// Load pair, immed post-index, normal
// Load pair, immed post-index, signed words
// NOTE: Handled by WriteLD, WriteLDHi, WriteAdr.

def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDNPDi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDNPQi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDNPSi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDNPWi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDNPXi)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDPDi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDPQi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDPSi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDPSWi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDPWi)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi], (instrs LDPXi)>;

def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDRBui)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDRDui)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDRHui)>;
def : InstRW<[THX2T99Write_5Cyc_LS01], (instrs LDRQui)>;
def : InstRW<[THX2T99Write_5Cyc_LS01], (instrs LDRSui)>;

def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDRDl)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDRQl)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDRWl)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDRXl)>;

def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRBi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRHi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRWi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRXi)>;

def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRSBWi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRSBXi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRSHWi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRSHXi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDTRSWi)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPDpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPQpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPSpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPWpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPWpre)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteAdr], (instrs LDRBpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteAdr], (instrs LDRDpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteAdr], (instrs LDRHpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteAdr], (instrs LDRQpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteAdr], (instrs LDRSpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteAdr], (instrs LDRWpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteAdr], (instrs LDRXpre)>;

def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRSBWpre)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRSBXpre)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRSBWpost)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRSBXpost)>;

def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRSHWpre)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRSHXpre)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRSHWpost)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRSHXpost)>;

def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRBBpre)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRBBpost)>;

def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRHHpre)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, WriteAdr], (instrs LDRHHpost)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPDpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPQpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPSpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPWpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteLDHi, WriteAdr],
            (instrs LDPXpost)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteI], (instrs LDRBpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteI], (instrs LDRDpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteI], (instrs LDRHpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteI], (instrs LDRQpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteI], (instrs LDRSpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteI], (instrs LDRWpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012, WriteI], (instrs LDRXpost)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPDpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPQpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPSpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPWpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPXpre)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteAdr], (instrs LDRBpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteAdr], (instrs LDRDpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteAdr], (instrs LDRHpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteAdr], (instrs LDRQpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteAdr], (instrs LDRSpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteAdr], (instrs LDRWpre)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteAdr], (instrs LDRXpre)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPDpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPQpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPSpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPWpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteLDHi, WriteAdr],
            (instrs LDPXpost)>;

def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteI], (instrs LDRBpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteI], (instrs LDRDpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteI], (instrs LDRHpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteI], (instrs LDRQpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteI], (instrs LDRSpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteI], (instrs LDRWpost)>;
def : InstRW<[THX2T99Write_5Cyc_LS01_I012_I012, WriteI], (instrs LDRXpost)>;

def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRBroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRDroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRHroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRHHroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRQroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRSroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRSHWroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRSHXroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRWroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRXroW)>;

def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRBroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRDroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRHHroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRHroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRQroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRSroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRSHWroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRSHXroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRWroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012, ReadAdrBase], (instrs LDRXroX)>;

def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRBroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRBroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
             (instrs LDRDroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRHroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRHHroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRQroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRSroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRSHWroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRSHXroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRWroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRXroW)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRBroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRDroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRHroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRHHroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRQroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRSroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRSHWroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRSHXroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRWroX)>;
def : InstRW<[THX2T99Write_4Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs LDRXroX)>;

def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURBi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURBBi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURDi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURHi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURHHi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURQi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURSi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURXi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURSBWi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURSBXi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURSHWi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURSHXi)>;
def : InstRW<[THX2T99Write_4Cyc_LS01], (instrs LDURSWi)>;

//---
// Prefetch
//---
def : InstRW<[THX2T99Write_6Cyc_LS01_I012], (instrs PRFMl)>;
def : InstRW<[THX2T99Write_6Cyc_LS01_I012], (instrs PRFUMi)>;
def : InstRW<[THX2T99Write_6Cyc_LS01_I012], (instrs PRFMui)>;
def : InstRW<[THX2T99Write_6Cyc_LS01_I012], (instrs PRFMroW)>;
def : InstRW<[THX2T99Write_6Cyc_LS01_I012], (instrs PRFMroX)>;

//--
// 3.7 Store Instructions
// 3.11 FP Store Instructions
//--

// Store register, unscaled immed
// Store register, immed unprivileged
// Store register, unsigned immed
def : WriteRes<WriteST,      [THX2T99LS01, THX2T99SD]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Store register, immed post-index
// NOTE: Handled by WriteAdr, WriteST, ReadAdrBase

// Store register, immed pre-index
// NOTE: Handled by WriteAdr, WriteST

// Store register, register offset, basic
// Store register, register offset, scaled by 4/8
// Store register, register offset, scaled by 2
// Store register, register offset, extend
// Store register, register offset, extend, scale by 4/8
// Store register, register offset, extend, scale by 1
def : WriteRes<WriteSTIdx, [THX2T99LS01, THX2T99SD, THX2T99I012]> {
  let Latency = 1;
  let NumMicroOps = 3;
}

// Store pair, immed offset, W-form
// Store pair, immed offset, X-form
def : WriteRes<WriteSTP,     [THX2T99LS01, THX2T99SD]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

// Store pair, immed post-index, W-form
// Store pair, immed post-index, X-form
// Store pair, immed pre-index, W-form
// Store pair, immed pre-index, X-form
// NOTE: Handled by WriteAdr, WriteSTP.

def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURBi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURBBi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURDi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURHi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURHHi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURQi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURSi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURWi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STURXi)>;

def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01], (instrs STTRBi)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01], (instrs STTRHi)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01], (instrs STTRWi)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01], (instrs STTRXi)>;

def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STNPDi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STNPQi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STNPXi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STNPWi)>;

def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STPDi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STPQi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STPXi)>;
def : InstRW<[THX2T99Write_1Cyc_LS01], (instrs STPWi)>;

def : InstRW<[THX2T99Write_1Cyc_LS01_I012_I012], (instrs STRBui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012], (instrs STRBui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012_I012], (instrs STRDui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012], (instrs STRDui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012_I012], (instrs STRHui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012], (instrs STRHui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012_I012], (instrs STRQui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012], (instrs STRQui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012_I012], (instrs STRXui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012], (instrs STRXui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012_I012], (instrs STRWui)>;
def : InstRW<[THX2T99Write_1Cyc_LS01_I012], (instrs STRWui)>;

def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STPDpre, STPDpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STPQpre, STPQpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STPSpre, STPSpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STPWpre, STPWpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STPXpre, STPXpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STPXpre, STPXpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STPXpre, STPXpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STPXpre, STPXpost)>;

def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRBpre, STRBpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRBBpre, STRBBpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRDpre, STRDpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRHpre, STRHpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRHHpre, STRHHpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRQpre, STRQpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRSpre, STRSpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRWpre, STRWpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012],
            (instrs STRXpre, STRXpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRXpre, STRXpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012],
            (instrs STRXpre, STRXpost)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRXpre, STRXpost)>;

def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRBroW, STRBroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRBroW, STRBroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRBBroW, STRBBroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRBBroW, STRBBroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRDroW, STRDroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRDroW, STRDroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRHroW, STRHroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRHroW, STRHroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRHHroW, STRHHroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRHHroW, STRHHroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRQroW, STRQroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRQroW, STRQroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRSroW, STRSroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRSroW, STRSroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRWroW, STRWroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRWroW, STRWroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012, ReadAdrBase],
            (instrs STRXroW, STRXroX)>;
def : InstRW<[WriteAdr, THX2T99Write_1Cyc_LS01_I012_I012, ReadAdrBase],
            (instrs STRXroW, STRXroX)>;

//---
// 3.8 FP Data Processing Instructions
//---

// FP absolute value
// FP min/max
// FP negate
def : WriteRes<WriteF,       [THX2T99F01]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// FP arithmetic
def : InstRW<[THX2T99Write_6Cyc_F01], (instregex "^FADD", "^FSUB")>;

// FP compare
def : WriteRes<WriteFCmp,    [THX2T99F01]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

// FP Mul, Div, Sqrt
def : WriteRes<WriteFDiv, [THX2T99F01]> {
  let Latency = 22;
  let ResourceCycles = [19];
}

def THX2T99XWriteFDiv : SchedWriteRes<[THX2T99F01]> {
  let Latency = 16;
  let ResourceCycles = [8];
  let NumMicroOps = 4;
}

def THX2T99XWriteFDivSP : SchedWriteRes<[THX2T99F01]> {
  let Latency = 16;
  let ResourceCycles = [8];
  let NumMicroOps = 4;
}

def THX2T99XWriteFDivDP : SchedWriteRes<[THX2T99F01]> {
  let Latency = 23;
  let ResourceCycles = [12];
  let NumMicroOps = 4;
}

def THX2T99XWriteFSqrtSP : SchedWriteRes<[THX2T99F01]> {
  let Latency = 16;
  let ResourceCycles = [8];
  let NumMicroOps = 4;
}

def THX2T99XWriteFSqrtDP : SchedWriteRes<[THX2T99F01]> {
  let Latency = 23;
  let ResourceCycles = [12];
  let NumMicroOps = 4;
}

// FP divide, S-form
// FP square root, S-form
def : InstRW<[THX2T99XWriteFDivSP], (instrs FDIVSrr)>;
def : InstRW<[THX2T99XWriteFSqrtSP], (instrs FSQRTSr)>;
def : InstRW<[THX2T99XWriteFDivSP], (instregex "^FDIVv.*32$")>;
def : InstRW<[THX2T99XWriteFSqrtSP], (instregex "^.*SQRT.*32$")>;
def : InstRW<[THX2T99Write_16Cyc_F01], (instregex "^FDIVSrr", "^FSQRTSr")>;

// FP divide, D-form
// FP square root, D-form
def : InstRW<[THX2T99XWriteFDivDP], (instrs FDIVDrr)>;
def : InstRW<[THX2T99XWriteFSqrtDP], (instrs FSQRTDr)>;
def : InstRW<[THX2T99XWriteFDivDP], (instregex "^FDIVv.*64$")>;
def : InstRW<[THX2T99XWriteFSqrtDP], (instregex "^.*SQRT.*64$")>;
def : InstRW<[THX2T99Write_23Cyc_F01], (instregex "^FDIVDrr", "^FSQRTDr")>;

// FP multiply
// FP multiply accumulate
def : WriteRes<WriteFMul, [THX2T99F01]> {
  let Latency = 6;
  let ResourceCycles = [2];
  let NumMicroOps = 3;
}

def THX2T99XWriteFMul : SchedWriteRes<[THX2T99F01]> {
  let Latency = 6;
  let ResourceCycles = [2];
  let NumMicroOps = 3;
}

def THX2T99XWriteFMulAcc : SchedWriteRes<[THX2T99F01]> {
  let Latency = 6;
  let ResourceCycles = [2];
  let NumMicroOps = 3;
}

def : InstRW<[THX2T99XWriteFMul], (instregex "^FMUL", "^FNMUL")>;
def : InstRW<[THX2T99XWriteFMulAcc],
            (instregex "^FMADD", "^FMSUB", "^FNMADD", "^FNMSUB")>;

// FP round to integral
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^FRINT(A|I|M|N|P|X|Z)(Sr|Dr)")>;

// FP select
def : InstRW<[THX2T99Write_4Cyc_F01], (instregex "^FCSEL")>;

//---
// 3.9 FP Miscellaneous Instructions
//---

// FP convert, from vec to vec reg
// FP convert, from gen to vec reg
// FP convert, from vec to gen reg
def : WriteRes<WriteFCvt, [THX2T99F01]> {
  let Latency = 7;
  let NumMicroOps = 3;
}

// FP move, immed
// FP move, register
def : WriteRes<WriteFImm, [THX2T99F01]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

// FP transfer, from gen to vec reg
// FP transfer, from vec to gen reg
def : WriteRes<WriteFCopy, [THX2T99F01]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def : InstRW<[THX2T99Write_5Cyc_F01], (instrs FMOVXDHighr, FMOVDXHighr)>;

//---
// 3.12 ASIMD Integer Instructions
//---

// ASIMD absolute diff, D-form
// ASIMD absolute diff, Q-form
// ASIMD absolute diff accum, D-form
// ASIMD absolute diff accum, Q-form
// ASIMD absolute diff accum long
// ASIMD absolute diff long
// ASIMD arith, basic
// ASIMD arith, complex
// ASIMD compare
// ASIMD logical (AND, BIC, EOR)
// ASIMD max/min, basic
// ASIMD max/min, reduce, 4H/4S
// ASIMD max/min, reduce, 8B/8H
// ASIMD max/min, reduce, 16B
// ASIMD multiply, D-form
// ASIMD multiply, Q-form
// ASIMD multiply accumulate long
// ASIMD multiply accumulate saturating long
// ASIMD multiply long
// ASIMD pairwise add and accumulate
// ASIMD shift accumulate
// ASIMD shift by immed, basic
// ASIMD shift by immed and insert, basic, D-form
// ASIMD shift by immed and insert, basic, Q-form
// ASIMD shift by immed, complex
// ASIMD shift by register, basic, D-form
// ASIMD shift by register, basic, Q-form
// ASIMD shift by register, complex, D-form
// ASIMD shift by register, complex, Q-form
def : WriteRes<WriteV, [THX2T99F01]> {
  let Latency = 7;
  let NumMicroOps = 4;
  let ResourceCycles = [4];
}

// ASIMD arith, reduce, 4H/4S
// ASIMD arith, reduce, 8B/8H
// ASIMD arith, reduce, 16B

// ASIMD logical (MVN (alias for NOT), ORN, ORR)
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "^ANDv", "^BICv", "^EORv", "^ORRv", "^ORNv", "^NOTv")>;

// ASIMD arith, reduce
def : InstRW<[THX2T99Write_10Cyc_F01],
            (instregex "^ADDVv", "^SADDLVv", "^UADDLVv")>;

// ASIMD polynomial (8x8) multiply long
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^(S|U|SQD)MULL")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "(S|U|SQD)(MLAL|MLSL|MULL)v.*")>;
def : InstRW<[THX2T99Write_5Cyc_F1], (instregex "^PMULL(v8i8|v16i8)")>;
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^PMULL(v1i64|v2i64)")>;

// ASIMD absolute diff accum, D-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^[SU]ABA(v8i8|v4i16|v2i32)$")>;
// ASIMD absolute diff accum, Q-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^[SU]ABA(v16i8|v8i16|v4i32)$")>;
// ASIMD absolute diff accum long
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^[SU]ABAL")>;
// ASIMD arith, reduce, 4H/4S
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "^[SU]?ADDL?V(v8i8|v4i16|v2i32)v$")>;
// ASIMD arith, reduce, 8B
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "^[SU]?ADDL?V(v8i16|v4i32)v$")>;
// ASIMD arith, reduce, 16B/16H
def : InstRW<[THX2T99Write_10Cyc_F01],
            (instregex "^[SU]?ADDL?Vv16i8v$")>;
// ASIMD max/min, reduce, 4H/4S
def : InstRW<[THX2T99Write_10Cyc_F01],
            (instregex "^[SU](MIN|MAX)V(v4i16|v4i32)v$")>;
// ASIMD max/min, reduce, 8B/8H
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^[SU](MIN|MAX)V(v8i8|v8i16)v$")>;
// ASIMD max/min, reduce, 16B/16H
def : InstRW<[THX2T99Write_10Cyc_F01],
            (instregex "^[SU](MIN|MAX)Vv16i8v$")>;
// ASIMD multiply, D-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^(P?MUL|SQR?DMULH)" #
                       "(v8i8|v4i16|v2i32|v1i8|v1i16|v1i32|v1i64)" #
                       "(_indexed)?$")>;
// ASIMD multiply, Q-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^(P?MUL|SQR?DMULH)(v16i8|v8i16|v4i32)(_indexed)?$")>;
// ASIMD multiply accumulate, D-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^ML[AS](v8i8|v4i16|v2i32)(_indexed)?$")>;
// ASIMD multiply accumulate, Q-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^ML[AS](v16i8|v8i16|v4i32)(_indexed)?$")>;
// ASIMD shift accumulate
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "SRSRAv","SSRAv","URSRAv","USRAv")>;

// ASIMD shift by immed, basic
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "RSHRNv","SHRNv", "SQRSHRNv","SQRSHRUNv",
                       "SQSHRNv","SQSHRUNv", "UQRSHRNv",
                       "UQSHRNv","SQXTNv","SQXTUNv","UQXTNv")>;
// ASIMD shift by immed, complex
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^[SU]?(Q|R){1,2}SHR")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^SQSHLU")>;
// ASIMD shift by register, basic, Q-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^[SU]SHL(v16i8|v8i16|v4i32|v2i64)")>;
// ASIMD shift by register, complex, D-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^[SU][QR]{1,2}SHL" #
                       "(v1i8|v1i16|v1i32|v1i64|v8i8|v4i16|v2i32|b|d|h|s)")>;
// ASIMD shift by register, complex, Q-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^[SU][QR]{1,2}SHL(v16i8|v8i16|v4i32|v2i64)")>;

// ASIMD Arithmetic
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "(ADD|SUB)(v8i8|v4i16|v2i32|v1i64)")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "(ADD|SUB)(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "(ADD|SUB)HNv.*")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "(RADD|RSUB)HNv.*")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^SQADD", "^SQNEG", "^SQSUB", "^SRHADD",
                       "^SUQADD", "^UQADD", "^UQSUB", "^URHADD", "^USQADD")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "ADDP(v16i8|v8i16|v4i32|v2i64)")>;
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "((AND|ORN|EOR|EON)S?(Xr[rsi]|v16i8|v8i16|v4i32)|" #
                       "(ORR|BIC)S?(Xr[rs]|v16i8|v8i16|v4i32))")>;
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "(CLS|CLZ|CNT)(v4i32|v8i16|v16i8)")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^SADALP","^UADALP")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^SADDLPv","^UADDLPv")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^SADDLV","^UADDLV")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
             (instregex "^ADDVv","^SMAXVv","^UMAXVv","^SMINVv","^UMINVv")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
             (instregex "^SABAv","^UABAv","^SABALv","^UABALv")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^SQADDv","^SQSUBv","^UQADDv","^UQSUBv")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^SUQADDv","^USQADDv")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^ADDHNv","^RADDHNv", "^RSUBHNv",
                       "^SQABS", "^SQADD", "^SQNEG", "^SQSUB",
                       "^SRHADD", "^SUBHNv", "^SUQADD",
                       "^UQADD", "^UQSUB", "^URHADD", "^USQADD")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^CMEQv","^CMGEv","^CMGTv",
                       "^CMLEv","^CMLTv", "^CMHIv","^CMHSv")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^SMAXv","^SMINv","^UMAXv","^UMINv",
                       "^SMAXPv","^SMINPv","^UMAXPv","^UMINPv")>;
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^SABDv","^UABDv", "^SABDLv","^UABDLv")>;

//---
// 3.13 ASIMD Floating-point Instructions
//---

// ASIMD FP absolute value
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^FABSv")>;

// ASIMD FP arith, normal, D-form
// ASIMD FP arith, normal, Q-form
def : InstRW<[THX2T99Write_6Cyc_F01],
            (instregex "^FABDv", "^FADDv", "^FSUBv")>;

// ASIMD FP arith,pairwise, D-form
// ASIMD FP arith, pairwise, Q-form
def : InstRW<[THX2T99Write_6Cyc_F01], (instregex "^FADDPv")>;

// ASIMD FP compare, D-form
// ASIMD FP compare, Q-form
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^FACGEv", "^FACGTv")>;
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^FCMEQv", "^FCMGEv",
                                                 "^FCMGTv", "^FCMLEv",
                                                 "^FCMLTv")>;

// ASIMD FP round, D-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^FRINT[AIMNPXZ](v2f32)")>;
// ASIMD FP round, Q-form
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^FRINT[AIMNPXZ](v4f32|v2f64)")>;

// ASIMD FP convert, long
// ASIMD FP convert, narrow
// ASIMD FP convert, other, D-form
// ASIMD FP convert, other, Q-form
// NOTE: Handled by WriteV.

// ASIMD FP convert, long and narrow
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^FCVT(L|N|XN)v")>;
// ASIMD FP convert, other, D-form
def : InstRW<[THX2T99Write_7Cyc_F01],
      (instregex "^[FVSU]CVT([AMNPZ][SU])?(_Int)?(v2f32|v1i32|v2i32|v1i64)")>;
// ASIMD FP convert, other, Q-form
def : InstRW<[THX2T99Write_7Cyc_F01],
      (instregex "^[FVSU]CVT([AMNPZ][SU])?(_Int)?(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP divide, D-form, F32
def : InstRW<[THX2T99Write_16Cyc_F01], (instrs FDIVv2f32)>;
def : InstRW<[THX2T99Write_16Cyc_F01], (instregex "FDIVv2f32")>;

// ASIMD FP divide, Q-form, F32
def : InstRW<[THX2T99Write_16Cyc_F01], (instrs FDIVv4f32)>;
def : InstRW<[THX2T99Write_16Cyc_F01], (instregex "FDIVv4f32")>;

// ASIMD FP divide, Q-form, F64
def : InstRW<[THX2T99Write_23Cyc_F01], (instrs FDIVv2f64)>;
def : InstRW<[THX2T99Write_23Cyc_F01], (instregex "FDIVv2f64")>;

// ASIMD FP max/min, normal, D-form
// ASIMD FP max/min, normal, Q-form
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^FMAXv", "^FMAXNMv",
                                                "^FMINv", "^FMINNMv")>;

// ASIMD FP max/min, pairwise, D-form
// ASIMD FP max/min, pairwise, Q-form
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^FMAXPv", "^FMAXNMPv",
                                                "^FMINPv", "^FMINNMPv")>;

// ASIMD FP max/min, reduce
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^FMAXVv", "^FMAXNMVv",
                                                "^FMINVv", "^FMINNMVv")>;

// ASIMD FP multiply, D-form, FZ
// ASIMD FP multiply, D-form, no FZ
// ASIMD FP multiply, Q-form, FZ
// ASIMD FP multiply, Q-form, no FZ
def : InstRW<[THX2T99Write_6Cyc_F01], (instregex "^FMULv", "^FMULXv")>;
def : InstRW<[THX2T99Write_6Cyc_F01],
            (instregex "^FMULX?(v2f32|v1i32|v2i32|v1i64|32|64)")>;
def : InstRW<[THX2T99Write_6Cyc_F01],
            (instregex "^FMULX?(v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP multiply accumulate, Dform, FZ
// ASIMD FP multiply accumulate, Dform, no FZ
// ASIMD FP multiply accumulate, Qform, FZ
// ASIMD FP multiply accumulate, Qform, no FZ
def : InstRW<[THX2T99Write_6Cyc_F01], (instregex "^FMLAv", "^FMLSv")>;
def : InstRW<[THX2T99Write_6Cyc_F01],
            (instregex "^FML[AS](v2f32|v1i32|v2i32|v1i64)")>;
def : InstRW<[THX2T99Write_6Cyc_F01],
            (instregex "^FML[AS](v4f32|v2f64|v4i32|v2i64)")>;

// ASIMD FP negate
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^FNEGv")>;

//--
// 3.14 ASIMD Miscellaneous Instructions
//--

// ASIMD bit reverse
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^RBITv")>;

// ASIMD bitwise insert, D-form
// ASIMD bitwise insert, Q-form
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "^BIFv", "^BITv", "^BSLv")>;

// ASIMD count, D-form
// ASIMD count, Q-form
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "^CLSv", "^CLZv", "^CNTv")>;

// ASIMD duplicate, gen reg
// ASIMD duplicate, element
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^DUPv")>;
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^CPY")>;
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^DUPv.+gpr")>;

// ASIMD extract
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^EXTv")>;

// ASIMD extract narrow
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^XTNv")>;

// ASIMD extract narrow, saturating
def : InstRW<[THX2T99Write_7Cyc_F01],
            (instregex "^SQXTNv", "^SQXTUNv", "^UQXTNv")>;

// ASIMD insert, element to element
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^INSv")>;

// ASIMD transfer, element to gen reg
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^[SU]MOVv")>;

// ASIMD move, integer immed
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^MOVIv")>;

// ASIMD move, FP immed
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^FMOVv")>;

// ASIMD table lookup, D-form
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^TB[LX]v8i8One")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^TB[LX]v8i8Two")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^TB[LX]v8i8Three")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^TB[LX]v8i8Four")>;

// ASIMD table lookup, Q-form
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^TB[LX]v16i8One")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^TB[LX]v16i8Two")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^TB[LX]v16i8Three")>;
def : InstRW<[THX2T99Write_7Cyc_F01], (instregex "^TB[LX]v16i8Four")>;

// ASIMD transpose
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^TRN1", "^TRN2")>;

// ASIMD unzip/zip
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "^UZP1", "^UZP2", "^ZIP1", "^ZIP2")>;

// ASIMD reciprocal estimate, D-form
// ASIMD reciprocal estimate, Q-form
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "^FRECPEv", "^FRECPXv", "^URECPEv",
                       "^FRSQRTEv", "^URSQRTEv")>;

// ASIMD reciprocal step, D-form, FZ
// ASIMD reciprocal step, D-form, no FZ
// ASIMD reciprocal step, Q-form, FZ
// ASIMD reciprocal step, Q-form, no FZ
def : InstRW<[THX2T99Write_6Cyc_F01], (instregex "^FRECPSv", "^FRSQRTSv")>;

// ASIMD reverse
def : InstRW<[THX2T99Write_5Cyc_F01],
            (instregex "^REV16v", "^REV32v", "^REV64v")>;

// ASIMD table lookup, D-form
// ASIMD table lookup, Q-form
def : InstRW<[THX2T99Write_8Cyc_F01], (instregex "^TBLv", "^TBXv")>;

// ASIMD transfer, element to word or word
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^[SU]MOVv")>;

// ASIMD transfer, element to gen reg
def : InstRW<[THX2T99Write_6Cyc_F01], (instregex "(S|U)MOVv.*")>;

// ASIMD transfer gen reg to element
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^INSv")>;

// ASIMD transpose
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^TRN1v", "^TRN2v",
                                                 "^UZP1v", "^UZP2v")>;

// ASIMD unzip/zip
def : InstRW<[THX2T99Write_5Cyc_F01], (instregex "^ZIP1v", "^ZIP2v")>;

//--
// 3.15 ASIMD Load Instructions
//--

// ASIMD load, 1 element, multiple, 1 reg, D-form
// ASIMD load, 1 element, multiple, 1 reg, Q-form
def : InstRW<[THX2T99Write_4Cyc_LS01],
            (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_4Cyc_LS01, WriteAdr],
            (instregex "^LD1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 2 reg, D-form
// ASIMD load, 1 element, multiple, 2 reg, Q-form
def : InstRW<[THX2T99Write_4Cyc_LS01],
            (instregex "^LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_4Cyc_LS01, WriteAdr],
            (instregex "^LD1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 3 reg, D-form
// ASIMD load, 1 element, multiple, 3 reg, Q-form
def : InstRW<[THX2T99Write_5Cyc_LS01],
            (instregex "^LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_5Cyc_LS01, WriteAdr],
            (instregex "^LD1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, multiple, 4 reg, D-form
// ASIMD load, 1 element, multiple, 4 reg, Q-form
def : InstRW<[THX2T99Write_6Cyc_LS01],
            (instregex "^LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_6Cyc_LS01, WriteAdr],
            (instregex "^LD1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 1 element, one lane, B/H/S
// ASIMD load, 1 element, one lane, D
def : InstRW<[THX2T99Write_5Cyc_LS01_F01], (instregex "^LD1i(8|16|32|64)$")>;
def : InstRW<[THX2T99Write_5Cyc_LS01_F01, WriteAdr],
            (instregex "^LD1i(8|16|32|64)_POST$")>;

// ASIMD load, 1 element, all lanes, D-form, B/H/S
// ASIMD load, 1 element, all lanes, D-form, D
// ASIMD load, 1 element, all lanes, Q-form
def : InstRW<[THX2T99Write_5Cyc_LS01_F01],
            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_5Cyc_LS01_F01, WriteAdr],
            (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, multiple, D-form, B/H/S
// ASIMD load, 2 element, multiple, Q-form, D
def : InstRW<[THX2T99Write_5Cyc_LS01_F01],
            (instregex "^LD2Twov(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_5Cyc_LS01_F01, WriteAdr],
            (instregex "^LD2Twov(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 2 element, one lane, B/H
// ASIMD load, 2 element, one lane, S
// ASIMD load, 2 element, one lane, D
def : InstRW<[THX2T99Write_5Cyc_LS01_F01], (instregex "^LD2i(8|16|32|64)$")>;
def : InstRW<[THX2T99Write_5Cyc_LS01_F01, WriteAdr],
            (instregex "^LD2i(8|16|32|64)_POST$")>;

// ASIMD load, 2 element, all lanes, D-form, B/H/S
// ASIMD load, 2 element, all lanes, D-form, D
// ASIMD load, 2 element, all lanes, Q-form
def : InstRW<[THX2T99Write_5Cyc_LS01_F01],
            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_5Cyc_LS01_F01, WriteAdr],
            (instregex "^LD2Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, multiple, D-form, B/H/S
// ASIMD load, 3 element, multiple, Q-form, B/H/S
// ASIMD load, 3 element, multiple, Q-form, D
def : InstRW<[THX2T99Write_8Cyc_LS01_F01],
            (instregex "^LD3Threev(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_8Cyc_LS01_F01, WriteAdr],
            (instregex "^LD3Threev(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 3 element, one lone, B/H
// ASIMD load, 3 element, one lane, S
// ASIMD load, 3 element, one lane, D
def : InstRW<[THX2T99Write_7Cyc_LS01_F01], (instregex "^LD3i(8|16|32|64)$")>;
def : InstRW<[THX2T99Write_7Cyc_LS01_F01, WriteAdr],
            (instregex "^LD3i(8|16|32|64)_POST$")>;

// ASIMD load, 3 element, all lanes, D-form, B/H/S
// ASIMD load, 3 element, all lanes, D-form, D
// ASIMD load, 3 element, all lanes, Q-form, B/H/S
// ASIMD load, 3 element, all lanes, Q-form, D
def : InstRW<[THX2T99Write_7Cyc_LS01_F01],
            (instregex "^LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_7Cyc_LS01_F01, WriteAdr],
            (instregex "^LD3Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, multiple, D-form, B/H/S
// ASIMD load, 4 element, multiple, Q-form, B/H/S
// ASIMD load, 4 element, multiple, Q-form, D
def : InstRW<[THX2T99Write_8Cyc_LS01_F01],
            (instregex "^LD4Fourv(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_8Cyc_LS01_F01, WriteAdr],
            (instregex "^LD4Fourv(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD load, 4 element, one lane, B/H
// ASIMD load, 4 element, one lane, S
// ASIMD load, 4 element, one lane, D
def : InstRW<[THX2T99Write_6Cyc_LS01_F01], (instregex "^LD4i(8|16|32|64)$")>;
def : InstRW<[THX2T99Write_6Cyc_LS01_F01, WriteAdr],
            (instregex "^LD4i(8|16|32|64)_POST$")>;

// ASIMD load, 4 element, all lanes, D-form, B/H/S
// ASIMD load, 4 element, all lanes, D-form, D
// ASIMD load, 4 element, all lanes, Q-form, B/H/S
// ASIMD load, 4 element, all lanes, Q-form, D
def : InstRW<[THX2T99Write_6Cyc_LS01_F01],
            (instregex "^LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_6Cyc_LS01_F01, WriteAdr],
            (instregex "^LD4Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

//--
// 3.16 ASIMD Store Instructions
//--

// ASIMD store, 1 element, multiple, 1 reg, D-form
// ASIMD store, 1 element, multiple, 1 reg, Q-form
def : InstRW<[THX2T99Write_1Cyc_LS01],
            (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01, WriteAdr],
            (instregex "^ST1Onev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 2 reg, D-form
// ASIMD store, 1 element, multiple, 2 reg, Q-form
def : InstRW<[THX2T99Write_1Cyc_LS01],
            (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01, WriteAdr],
            (instregex "^ST1Twov(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 3 reg, D-form
// ASIMD store, 1 element, multiple, 3 reg, Q-form
def : InstRW<[THX2T99Write_1Cyc_LS01],
            (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01, WriteAdr],
            (instregex "^ST1Threev(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, multiple, 4 reg, D-form
// ASIMD store, 1 element, multiple, 4 reg, Q-form
def : InstRW<[THX2T99Write_1Cyc_LS01],
            (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01, WriteAdr],
            (instregex "^ST1Fourv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 1 element, one lane, B/H/S
// ASIMD store, 1 element, one lane, D
def : InstRW<[THX2T99Write_1Cyc_LS01_F01],
            (instregex "^ST1i(8|16|32|64)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01_F01, WriteAdr],
            (instregex "^ST1i(8|16|32|64)_POST$")>;

// ASIMD store, 2 element, multiple, D-form, B/H/S
// ASIMD store, 2 element, multiple, Q-form, B/H/S
// ASIMD store, 2 element, multiple, Q-form, D
def : InstRW<[THX2T99Write_1Cyc_LS01_F01],
            (instregex "^ST2Twov(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01_F01, WriteAdr],
            (instregex "^ST2Twov(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 2 element, one lane, B/H/S
// ASIMD store, 2 element, one lane, D
def : InstRW<[THX2T99Write_1Cyc_LS01_F01],
            (instregex "^ST2i(8|16|32|64)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01_F01, WriteAdr],
            (instregex "^ST2i(8|16|32|64)_POST$")>;

// ASIMD store, 3 element, multiple, D-form, B/H/S
// ASIMD store, 3 element, multiple, Q-form, B/H/S
// ASIMD store, 3 element, multiple, Q-form, D
def : InstRW<[THX2T99Write_1Cyc_LS01_F01],
            (instregex "^ST3Threev(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01_F01, WriteAdr],
            (instregex "^ST3Threev(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 3 element, one lane, B/H
// ASIMD store, 3 element, one lane, S
// ASIMD store, 3 element, one lane, D
def : InstRW<[THX2T99Write_1Cyc_LS01_F01], (instregex "^ST3i(8|16|32|64)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01_F01, WriteAdr],
            (instregex "^ST3i(8|16|32|64)_POST$")>;

// ASIMD store, 4 element, multiple, D-form, B/H/S
// ASIMD store, 4 element, multiple, Q-form, B/H/S
// ASIMD store, 4 element, multiple, Q-form, D
def : InstRW<[THX2T99Write_1Cyc_LS01_F01],
            (instregex "^ST4Fourv(8b|4h|2s|16b|8h|4s|2d)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01_F01, WriteAdr],
            (instregex "^ST4Fourv(8b|4h|2s|16b|8h|4s|2d)_POST$")>;

// ASIMD store, 4 element, one lane, B/H
// ASIMD store, 4 element, one lane, S
// ASIMD store, 4 element, one lane, D
def : InstRW<[THX2T99Write_1Cyc_LS01_F01], (instregex "^ST4i(8|16|32|64)$")>;
def : InstRW<[THX2T99Write_1Cyc_LS01_F01, WriteAdr],
            (instregex "^ST4i(8|16|32|64)_POST$")>;

// V8.1a Atomics (LSE)
def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs CASB, CASH, CASW, CASX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs CASAB, CASAH, CASAW, CASAX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs CASLB, CASLH, CASLW, CASLX)>;

def : InstRW<[THX2T99Write_16Cyc_I012, WriteAtomic],
            (instrs CASALB, CASALH, CASALW, CASALX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDLARB, LDLARH, LDLARW, LDLARX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs LDADDB, LDADDH, LDADDW, LDADDX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDADDAB, LDADDAH, LDADDAW, LDADDAX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDADDLB, LDADDLH, LDADDLW, LDADDLX)>;

def : InstRW<[THX2T99Write_16Cyc_I012, WriteAtomic],
            (instrs LDADDALB, LDADDALH, LDADDALW, LDADDALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs LDCLRB, LDCLRH, LDCLRW, LDCLRX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDCLRAB, LDCLRAH, LDCLRAW, LDCLRAX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDCLRLB, LDCLRLH, LDCLRLW, LDCLRLX)>;

def : InstRW<[THX2T99Write_16Cyc_I012, WriteAtomic],
            (instrs LDCLRALB, LDCLRALH, LDCLRALW, LDCLRALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs LDEORB, LDEORH, LDEORW, LDEORX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDEORAB, LDEORAH, LDEORAW, LDEORAX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDEORLB, LDEORLH, LDEORLW, LDEORLX)>;

def : InstRW<[THX2T99Write_16Cyc_I012, WriteAtomic],
            (instrs LDEORALB, LDEORALH, LDEORALW, LDEORALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs LDSETB, LDSETH, LDSETW, LDSETX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDSETAB, LDSETAH, LDSETAW, LDSETAX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs LDSETLB, LDSETLH, LDSETLW, LDSETLX)>;

def : InstRW<[THX2T99Write_16Cyc_I012, WriteAtomic],
            (instrs LDSETALB, LDSETALH, LDSETALW, LDSETALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs LDSMAXB, LDSMAXH, LDSMAXW, LDSMAXX,
             LDSMAXAB, LDSMAXAH, LDSMAXAW, LDSMAXAX,
             LDSMAXLB, LDSMAXLH, LDSMAXLW, LDSMAXLX,
             LDSMAXALB, LDSMAXALH, LDSMAXALW, LDSMAXALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs LDSMINB, LDSMINH, LDSMINW, LDSMINX,
             LDSMINAB, LDSMINAH, LDSMINAW, LDSMINAX,
             LDSMINLB, LDSMINLH, LDSMINLW, LDSMINLX,
             LDSMINALB, LDSMINALH, LDSMINALW, LDSMINALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs LDUMAXB, LDUMAXH, LDUMAXW, LDUMAXX,
             LDUMAXAB, LDUMAXAH, LDUMAXAW, LDUMAXAX,
             LDUMAXLB, LDUMAXLH, LDUMAXLW, LDUMAXLX,
             LDUMAXALB, LDUMAXALH, LDUMAXALW, LDUMAXALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs LDUMINB, LDUMINH, LDUMINW, LDUMINX,
             LDUMINAB, LDUMINAH, LDUMINAW, LDUMINAX,
             LDUMINLB, LDUMINLH, LDUMINLW, LDUMINLX,
             LDUMINALB, LDUMINALH, LDUMINALW, LDUMINALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs SWPB, SWPH, SWPW, SWPX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs SWPAB, SWPAH, SWPAW, SWPAX)>;

def : InstRW<[THX2T99Write_12Cyc_I012, WriteAtomic],
            (instrs SWPLB, SWPLH, SWPLW, SWPLX)>;

def : InstRW<[THX2T99Write_16Cyc_I012, WriteAtomic],
            (instrs SWPALB, SWPALH, SWPALW, SWPALX)>;

def : InstRW<[THX2T99Write_8Cyc_I012, WriteAtomic],
            (instrs STLLRB, STLLRH, STLLRW, STLLRX)>;

} // SchedModel = ThunderX2T99Model

