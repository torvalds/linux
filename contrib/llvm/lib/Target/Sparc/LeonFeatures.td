//===-- LeonFeatures.td - Describe the Leon Features -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//


//===----------------------------------------------------------------------===//
// UMAC and SMAC support for LEON3 and LEON4 processors.
//===----------------------------------------------------------------------===//

//support to casa instruction; for leon3 subtarget only
def UMACSMACSupport : SubtargetFeature<
  "hasumacsmac", 
  "HasUmacSmac", 
  "true", 
  "Enable UMAC and SMAC for LEON3 and LEON4 processors"
>;


//===----------------------------------------------------------------------===//
// CASA Support differs between LEON3-FT GR712RC and LEON3-FT UT699
// We need to have the option to switch this on and off.
//===----------------------------------------------------------------------===//

//support to casa instruction; for leon3 subtarget only
def LeonCASA : SubtargetFeature<
  "hasleoncasa", 
  "HasLeonCasa", 
  "true", 
  "Enable CASA instruction for LEON3 and LEON4 processors"
>;

def InsertNOPLoad: SubtargetFeature<
  "insertnopload",
  "InsertNOPLoad",
  "true",
  "LEON3 erratum fix: Insert a NOP instruction after every single-cycle load instruction when the next instruction is another load/store instruction" 
>;

def DetectRoundChange : SubtargetFeature<
  "detectroundchange",
  "DetectRoundChange",
  "true",
  "LEON3 erratum detection: Detects any rounding mode change "
  "request: use only the round-to-nearest rounding mode"
>;

def FixAllFDIVSQRT : SubtargetFeature<
  "fixallfdivsqrt",
  "FixAllFDIVSQRT",
  "true",
  "LEON erratum fix: Fix FDIVS/FDIVD/FSQRTS/FSQRTD instructions with NOPs and floating-point store" 
>;

def LeonCycleCounter
  : SubtargetFeature<"leoncyclecounter", "HasLeonCycleCounter", "true",
                     "Use the Leon cycle counter register">;
