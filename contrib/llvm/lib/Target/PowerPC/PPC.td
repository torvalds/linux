//===-- PPC.td - Describe the PowerPC Target Machine -------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This is the top level entry point for the PowerPC target.
//
//===----------------------------------------------------------------------===//

// Get the target-independent interfaces which we are implementing.
//
include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// PowerPC Subtarget features.
//

//===----------------------------------------------------------------------===//
// CPU Directives                                                             //
//===----------------------------------------------------------------------===//

def Directive440 : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_440", "">;
def Directive601 : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_601", "">;
def Directive602 : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_602", "">;
def Directive603 : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_603", "">;
def Directive604 : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_603", "">;
def Directive620 : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_603", "">;
def Directive7400: SubtargetFeature<"", "DarwinDirective", "PPC::DIR_7400", "">;
def Directive750 : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_750", "">;
def Directive970 : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_970", "">;
def Directive32  : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_32", "">;
def Directive64  : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_64", "">;
def DirectiveA2  : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_A2", "">;
def DirectiveE500   : SubtargetFeature<"", "DarwinDirective",
                                       "PPC::DIR_E500", "">;
def DirectiveE500mc : SubtargetFeature<"", "DarwinDirective",
                                       "PPC::DIR_E500mc", "">;
def DirectiveE5500  : SubtargetFeature<"", "DarwinDirective",
                                       "PPC::DIR_E5500", "">;
def DirectivePwr3: SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR3", "">;
def DirectivePwr4: SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR4", "">;
def DirectivePwr5: SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR5", "">;
def DirectivePwr5x
    : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR5X", "">;
def DirectivePwr6: SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR6", "">;
def DirectivePwr6x
    : SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR6X", "">;
def DirectivePwr7: SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR7", "">;
def DirectivePwr8: SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR8", "">;
def DirectivePwr9: SubtargetFeature<"", "DarwinDirective", "PPC::DIR_PWR9", "">;

def Feature64Bit     : SubtargetFeature<"64bit","Has64BitSupport", "true",
                                        "Enable 64-bit instructions">;
def FeatureHardFloat : SubtargetFeature<"hard-float", "HasHardFloat", "true",
                              "Enable floating-point instructions">;
def Feature64BitRegs : SubtargetFeature<"64bitregs","Use64BitRegs", "true",
                              "Enable 64-bit registers usage for ppc32 [beta]">;
def FeatureCRBits    : SubtargetFeature<"crbits", "UseCRBits", "true",
                              "Use condition-register bits individually">;
def FeatureFPU       : SubtargetFeature<"fpu","HasFPU","true",
                                        "Enable classic FPU instructions",
                                        [FeatureHardFloat]>;
def FeatureAltivec   : SubtargetFeature<"altivec","HasAltivec", "true",
                                        "Enable Altivec instructions",
                                        [FeatureFPU]>;
def FeatureSPE       : SubtargetFeature<"spe","HasSPE", "true",
                                        "Enable SPE instructions",
                                        [FeatureHardFloat]>;
def FeatureMFOCRF    : SubtargetFeature<"mfocrf","HasMFOCRF", "true",
                                        "Enable the MFOCRF instruction">;
def FeatureFSqrt     : SubtargetFeature<"fsqrt","HasFSQRT", "true",
                                        "Enable the fsqrt instruction",
                                        [FeatureFPU]>;
def FeatureFCPSGN    : SubtargetFeature<"fcpsgn", "HasFCPSGN", "true",
                                        "Enable the fcpsgn instruction",
                                        [FeatureFPU]>;
def FeatureFRE       : SubtargetFeature<"fre", "HasFRE", "true",
                                        "Enable the fre instruction",
                                        [FeatureFPU]>;
def FeatureFRES      : SubtargetFeature<"fres", "HasFRES", "true",
                                        "Enable the fres instruction",
                                        [FeatureFPU]>;
def FeatureFRSQRTE   : SubtargetFeature<"frsqrte", "HasFRSQRTE", "true",
                                        "Enable the frsqrte instruction",
                                        [FeatureFPU]>;
def FeatureFRSQRTES  : SubtargetFeature<"frsqrtes", "HasFRSQRTES", "true",
                                        "Enable the frsqrtes instruction",
                                        [FeatureFPU]>;
def FeatureRecipPrec : SubtargetFeature<"recipprec", "HasRecipPrec", "true",
                              "Assume higher precision reciprocal estimates">;
def FeatureSTFIWX    : SubtargetFeature<"stfiwx","HasSTFIWX", "true",
                                        "Enable the stfiwx instruction",
                                        [FeatureFPU]>;
def FeatureLFIWAX    : SubtargetFeature<"lfiwax","HasLFIWAX", "true",
                                        "Enable the lfiwax instruction",
                                        [FeatureFPU]>;
def FeatureFPRND     : SubtargetFeature<"fprnd", "HasFPRND", "true",
                                        "Enable the fri[mnpz] instructions",
                                        [FeatureFPU]>;
def FeatureFPCVT     : SubtargetFeature<"fpcvt", "HasFPCVT", "true",
  "Enable fc[ft]* (unsigned and single-precision) and lfiwzx instructions",
                                        [FeatureFPU]>;
def FeatureISEL      : SubtargetFeature<"isel","HasISEL", "true",
                                        "Enable the isel instruction">;
def FeatureBPERMD    : SubtargetFeature<"bpermd", "HasBPERMD", "true",
                                        "Enable the bpermd instruction">;
def FeatureExtDiv    : SubtargetFeature<"extdiv", "HasExtDiv", "true",
                                        "Enable extended divide instructions">;
def FeatureLDBRX     : SubtargetFeature<"ldbrx","HasLDBRX", "true",
                                        "Enable the ldbrx instruction">;
def FeatureCMPB      : SubtargetFeature<"cmpb", "HasCMPB", "true",
                                        "Enable the cmpb instruction">;
def FeatureICBT      : SubtargetFeature<"icbt","HasICBT", "true",
                                        "Enable icbt instruction">;
def FeatureBookE     : SubtargetFeature<"booke", "IsBookE", "true",
                                        "Enable Book E instructions",
                                        [FeatureICBT]>;
def FeatureMSYNC     : SubtargetFeature<"msync", "HasOnlyMSYNC", "true",
                              "Has only the msync instruction instead of sync",
                              [FeatureBookE]>;
def FeatureE500      : SubtargetFeature<"e500", "IsE500", "true",
                                        "Enable E500/E500mc instructions">;
def FeatureSecurePlt : SubtargetFeature<"secure-plt","SecurePlt", "true",
                                        "Enable secure plt mode">;
def FeaturePPC4xx    : SubtargetFeature<"ppc4xx", "IsPPC4xx", "true",
                                        "Enable PPC 4xx instructions">;
def FeaturePPC6xx    : SubtargetFeature<"ppc6xx", "IsPPC6xx", "true",
                                        "Enable PPC 6xx instructions">;
def FeatureQPX       : SubtargetFeature<"qpx","HasQPX", "true",
                                        "Enable QPX instructions",
                                        [FeatureFPU]>;
def FeatureVSX       : SubtargetFeature<"vsx","HasVSX", "true",
                                        "Enable VSX instructions",
                                        [FeatureAltivec]>;
def FeatureP8Altivec : SubtargetFeature<"power8-altivec", "HasP8Altivec", "true",
                                        "Enable POWER8 Altivec instructions",
                                        [FeatureAltivec]>;
def FeatureP8Crypto : SubtargetFeature<"crypto", "HasP8Crypto", "true",
                                       "Enable POWER8 Crypto instructions",
                                       [FeatureP8Altivec]>;
def FeatureP8Vector  : SubtargetFeature<"power8-vector", "HasP8Vector", "true",
                                        "Enable POWER8 vector instructions",
                                        [FeatureVSX, FeatureP8Altivec]>;
def FeatureDirectMove :
  SubtargetFeature<"direct-move", "HasDirectMove", "true",
                   "Enable Power8 direct move instructions",
                   [FeatureVSX]>;
def FeaturePartwordAtomic : SubtargetFeature<"partword-atomics",
                                             "HasPartwordAtomics", "true",
                                             "Enable l[bh]arx and st[bh]cx.">;
def FeatureInvariantFunctionDescriptors :
  SubtargetFeature<"invariant-function-descriptors",
                   "HasInvariantFunctionDescriptors", "true",
                   "Assume function descriptors are invariant">;
def FeatureLongCall : SubtargetFeature<"longcall", "UseLongCalls", "true",
                                       "Always use indirect calls">;
def FeatureHTM : SubtargetFeature<"htm", "HasHTM", "true",
                                  "Enable Hardware Transactional Memory instructions">;
def FeatureMFTB   : SubtargetFeature<"", "FeatureMFTB", "true",
                                        "Implement mftb using the mfspr instruction">;
def FeatureFusion : SubtargetFeature<"fusion", "HasFusion", "true",
                                     "Target supports add/load integer fusion.">;
def FeatureFloat128 :
  SubtargetFeature<"float128", "HasFloat128", "true",
                   "Enable the __float128 data type for IEEE-754R Binary128.",
                   [FeatureVSX]>;
def FeaturePOPCNTD   : SubtargetFeature<"popcntd","HasPOPCNTD",
                                        "POPCNTD_Fast",
                                        "Enable the popcnt[dw] instructions">;
// Note that for the a2/a2q processor models we should not use popcnt[dw] by
// default. These processors do support the instructions, but they're
// microcoded, and the software emulation is about twice as fast.
def FeatureSlowPOPCNTD : SubtargetFeature<"slow-popcntd","HasPOPCNTD",
                                          "POPCNTD_Slow",
                                          "Has slow popcnt[dw] instructions">;

def DeprecatedDST    : SubtargetFeature<"", "DeprecatedDST", "true",
  "Treat vector data stream cache control instructions as deprecated">;

def FeatureISA3_0 : SubtargetFeature<"isa-v30-instructions", "IsISA3_0",
                                     "true",
                                     "Enable instructions added in ISA 3.0.">;
def FeatureP9Altivec : SubtargetFeature<"power9-altivec", "HasP9Altivec", "true",
                                        "Enable POWER9 Altivec instructions",
                                        [FeatureISA3_0, FeatureP8Altivec]>;
def FeatureP9Vector  : SubtargetFeature<"power9-vector", "HasP9Vector", "true",
                                        "Enable POWER9 vector instructions",
                                        [FeatureISA3_0, FeatureP8Vector,
                                         FeatureP9Altivec]>;

// Since new processors generally contain a superset of features of those that
// came before them, the idea is to make implementations of new processors
// less error prone and easier to read.
// Namely:
//     list<SubtargetFeature> Power8FeatureList = ...
//     list<SubtargetFeature> FutureProcessorSpecificFeatureList =
//         [ features that Power8 does not support ]
//     list<SubtargetFeature> FutureProcessorFeatureList =
//         !listconcat(Power8FeatureList, FutureProcessorSpecificFeatureList)

// Makes it explicit and obvious what is new in FutureProcesor vs. Power8 as
// well as providing a single point of definition if the feature set will be
// used elsewhere.
def ProcessorFeatures {
  list<SubtargetFeature> Power7FeatureList =
      [DirectivePwr7, FeatureAltivec, FeatureVSX,
       FeatureMFOCRF, FeatureFCPSGN, FeatureFSqrt, FeatureFRE,
       FeatureFRES, FeatureFRSQRTE, FeatureFRSQRTES,
       FeatureRecipPrec, FeatureSTFIWX, FeatureLFIWAX,
       FeatureFPRND, FeatureFPCVT, FeatureISEL,
       FeaturePOPCNTD, FeatureCMPB, FeatureLDBRX,
       Feature64Bit /*, Feature64BitRegs */,
       FeatureBPERMD, FeatureExtDiv,
       FeatureMFTB, DeprecatedDST];
  list<SubtargetFeature> Power8SpecificFeatures =
      [DirectivePwr8, FeatureP8Altivec, FeatureP8Vector, FeatureP8Crypto,
       FeatureHTM, FeatureDirectMove, FeatureICBT, FeaturePartwordAtomic,
       FeatureFusion];
  list<SubtargetFeature> Power8FeatureList =
      !listconcat(Power7FeatureList, Power8SpecificFeatures);
  list<SubtargetFeature> Power9SpecificFeatures =
      [DirectivePwr9, FeatureP9Altivec, FeatureP9Vector, FeatureISA3_0];
  list<SubtargetFeature> Power9FeatureList =
      !listconcat(Power8FeatureList, Power9SpecificFeatures);
}

// Note: Future features to add when support is extended to more
// recent ISA levels:
//
// DFP          p6, p6x, p7        decimal floating-point instructions
// POPCNTB      p5 through p7      popcntb and related instructions

//===----------------------------------------------------------------------===//
// Classes used for relation maps.
//===----------------------------------------------------------------------===//
// RecFormRel - Filter class used to relate non-record-form instructions with
// their record-form variants.
class RecFormRel;

// AltVSXFMARel - Filter class used to relate the primary addend-killing VSX
// FMA instruction forms with their corresponding factor-killing forms.
class AltVSXFMARel {
  bit IsVSXFMAAlt = 0;
}

//===----------------------------------------------------------------------===//
// Relation Map Definitions.
//===----------------------------------------------------------------------===//

def getRecordFormOpcode : InstrMapping {
  let FilterClass = "RecFormRel";
  // Instructions with the same BaseName and Interpretation64Bit values
  // form a row.
  let RowFields = ["BaseName", "Interpretation64Bit"];
  // Instructions with the same RC value form a column.
  let ColFields = ["RC"];
  // The key column are the non-record-form instructions.
  let KeyCol = ["0"];
  // Value columns RC=1
  let ValueCols = [["1"]];
}

def getNonRecordFormOpcode : InstrMapping {
  let FilterClass = "RecFormRel";
  // Instructions with the same BaseName and Interpretation64Bit values
  // form a row.
  let RowFields = ["BaseName", "Interpretation64Bit"];
  // Instructions with the same RC value form a column.
  let ColFields = ["RC"];
  // The key column are the record-form instructions.
  let KeyCol = ["1"];
  // Value columns are RC=0
  let ValueCols = [["0"]];
}

def getAltVSXFMAOpcode : InstrMapping {
  let FilterClass = "AltVSXFMARel";
  // Instructions with the same BaseName and Interpretation64Bit values
  // form a row.
  let RowFields = ["BaseName"];
  // Instructions with the same RC value form a column.
  let ColFields = ["IsVSXFMAAlt"];
  // The key column are the (default) addend-killing instructions.
  let KeyCol = ["0"];
  // Value columns IsVSXFMAAlt=1
  let ValueCols = [["1"]];
}

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "PPCRegisterInfo.td"
include "PPCSchedule.td"

//===----------------------------------------------------------------------===//
// PowerPC processors supported.
//

def : Processor<"generic", G3Itineraries, [Directive32, FeatureHardFloat,
                                           FeatureMFTB]>;
def : ProcessorModel<"440", PPC440Model, [Directive440, FeatureISEL,
                                          FeatureFRES, FeatureFRSQRTE,
                                          FeatureICBT, FeatureBookE,
                                          FeatureMSYNC, FeatureMFTB]>;
def : ProcessorModel<"450", PPC440Model, [Directive440, FeatureISEL,
                                          FeatureFRES, FeatureFRSQRTE,
                                          FeatureICBT, FeatureBookE,
                                          FeatureMSYNC, FeatureMFTB]>;
def : Processor<"601", G3Itineraries, [Directive601, FeatureFPU]>;
def : Processor<"602", G3Itineraries, [Directive602, FeatureFPU,
                                       FeatureMFTB]>;
def : Processor<"603", G3Itineraries, [Directive603,
                                       FeatureFRES, FeatureFRSQRTE,
                                       FeatureMFTB]>;
def : Processor<"603e", G3Itineraries, [Directive603,
                                        FeatureFRES, FeatureFRSQRTE,
                                        FeatureMFTB]>;
def : Processor<"603ev", G3Itineraries, [Directive603,
                                         FeatureFRES, FeatureFRSQRTE,
                                         FeatureMFTB]>;
def : Processor<"604", G3Itineraries, [Directive604,
                                       FeatureFRES, FeatureFRSQRTE,
                                       FeatureMFTB]>;
def : Processor<"604e", G3Itineraries, [Directive604,
                                        FeatureFRES, FeatureFRSQRTE,
                                        FeatureMFTB]>;
def : Processor<"620", G3Itineraries, [Directive620,
                                       FeatureFRES, FeatureFRSQRTE,
                                       FeatureMFTB]>;
def : Processor<"750", G4Itineraries, [Directive750,
                                       FeatureFRES, FeatureFRSQRTE,
                                       FeatureMFTB]>;
def : Processor<"g3", G3Itineraries, [Directive750,
                                      FeatureFRES, FeatureFRSQRTE,
                                      FeatureMFTB]>;
def : Processor<"7400", G4Itineraries, [Directive7400, FeatureAltivec,
                                        FeatureFRES, FeatureFRSQRTE,
                                        FeatureMFTB]>;
def : Processor<"g4", G4Itineraries, [Directive7400, FeatureAltivec,
                                      FeatureFRES, FeatureFRSQRTE,
                                      FeatureMFTB]>;
def : Processor<"7450", G4PlusItineraries, [Directive7400, FeatureAltivec,
                                            FeatureFRES, FeatureFRSQRTE,
                                            FeatureMFTB]>;
def : Processor<"g4+", G4PlusItineraries, [Directive7400, FeatureAltivec,
                                           FeatureFRES, FeatureFRSQRTE,
                                           FeatureMFTB]>;

def : ProcessorModel<"970", G5Model,
                  [Directive970, FeatureAltivec,
                   FeatureMFOCRF, FeatureFSqrt,
                   FeatureFRES, FeatureFRSQRTE, FeatureSTFIWX,
                   Feature64Bit /*, Feature64BitRegs */,
                   FeatureMFTB]>;
def : ProcessorModel<"g5", G5Model,
                  [Directive970, FeatureAltivec,
                   FeatureMFOCRF, FeatureFSqrt, FeatureSTFIWX,
                   FeatureFRES, FeatureFRSQRTE,
                   Feature64Bit /*, Feature64BitRegs */,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"e500", PPCE500Model,
                  [DirectiveE500,
                   FeatureICBT, FeatureBookE,
                   FeatureISEL, FeatureMFTB]>;
def : ProcessorModel<"e500mc", PPCE500mcModel,
                  [DirectiveE500mc,
                   FeatureSTFIWX, FeatureICBT, FeatureBookE,
                   FeatureISEL, FeatureMFTB]>;
def : ProcessorModel<"e5500", PPCE5500Model,
                  [DirectiveE5500, FeatureMFOCRF, Feature64Bit,
                   FeatureSTFIWX, FeatureICBT, FeatureBookE,
                   FeatureISEL, FeatureMFTB]>;
def : ProcessorModel<"a2", PPCA2Model,
                  [DirectiveA2, FeatureICBT, FeatureBookE, FeatureMFOCRF,
                   FeatureFCPSGN, FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES, FeatureRecipPrec,
                   FeatureSTFIWX, FeatureLFIWAX,
                   FeatureFPRND, FeatureFPCVT, FeatureISEL,
                   FeatureSlowPOPCNTD, FeatureCMPB, FeatureLDBRX,
                   Feature64Bit /*, Feature64BitRegs */, FeatureMFTB]>;
def : ProcessorModel<"a2q", PPCA2Model,
                  [DirectiveA2, FeatureICBT, FeatureBookE, FeatureMFOCRF,
                   FeatureFCPSGN, FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES, FeatureRecipPrec,
                   FeatureSTFIWX, FeatureLFIWAX,
                   FeatureFPRND, FeatureFPCVT, FeatureISEL,
                   FeatureSlowPOPCNTD, FeatureCMPB, FeatureLDBRX,
                   Feature64Bit /*, Feature64BitRegs */, FeatureQPX,
                   FeatureMFTB]>;
def : ProcessorModel<"pwr3", G5Model,
                  [DirectivePwr3, FeatureAltivec,
                   FeatureFRES, FeatureFRSQRTE, FeatureMFOCRF,
                   FeatureSTFIWX, Feature64Bit]>;
def : ProcessorModel<"pwr4", G5Model,
                  [DirectivePwr4, FeatureAltivec, FeatureMFOCRF,
                   FeatureFSqrt, FeatureFRES, FeatureFRSQRTE,
                   FeatureSTFIWX, Feature64Bit, FeatureMFTB]>;
def : ProcessorModel<"pwr5", G5Model,
                  [DirectivePwr5, FeatureAltivec, FeatureMFOCRF,
                   FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES,
                   FeatureSTFIWX, Feature64Bit,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"pwr5x", G5Model,
                  [DirectivePwr5x, FeatureAltivec, FeatureMFOCRF,
                   FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES,
                   FeatureSTFIWX, FeatureFPRND, Feature64Bit,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"pwr6", G5Model,
                  [DirectivePwr6, FeatureAltivec,
                   FeatureMFOCRF, FeatureFCPSGN, FeatureFSqrt, FeatureFRE,
                   FeatureFRES, FeatureFRSQRTE, FeatureFRSQRTES,
                   FeatureRecipPrec, FeatureSTFIWX, FeatureLFIWAX, FeatureCMPB,
                   FeatureFPRND, Feature64Bit /*, Feature64BitRegs */,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"pwr6x", G5Model,
                  [DirectivePwr5x, FeatureAltivec, FeatureMFOCRF,
                   FeatureFCPSGN, FeatureFSqrt, FeatureFRE, FeatureFRES,
                   FeatureFRSQRTE, FeatureFRSQRTES, FeatureRecipPrec,
                   FeatureSTFIWX, FeatureLFIWAX, FeatureCMPB,
                   FeatureFPRND, Feature64Bit,
                   FeatureMFTB, DeprecatedDST]>;
def : ProcessorModel<"pwr7", P7Model, ProcessorFeatures.Power7FeatureList>;
def : ProcessorModel<"pwr8", P8Model, ProcessorFeatures.Power8FeatureList>;
def : ProcessorModel<"pwr9", P9Model, ProcessorFeatures.Power9FeatureList>;
def : Processor<"ppc", G3Itineraries, [Directive32, FeatureHardFloat,
                                       FeatureMFTB]>;
def : Processor<"ppc32", G3Itineraries, [Directive32, FeatureHardFloat,
                                         FeatureMFTB]>;
def : ProcessorModel<"ppc64", G5Model,
                  [Directive64, FeatureAltivec,
                   FeatureMFOCRF, FeatureFSqrt, FeatureFRES,
                   FeatureFRSQRTE, FeatureSTFIWX,
                   Feature64Bit /*, Feature64BitRegs */,
                   FeatureMFTB]>;
def : ProcessorModel<"ppc64le", P8Model, ProcessorFeatures.Power8FeatureList>;

//===----------------------------------------------------------------------===//
// Calling Conventions
//===----------------------------------------------------------------------===//

include "PPCCallingConv.td"

def PPCInstrInfo : InstrInfo {
  let isLittleEndianEncoding = 1;

  // FIXME: Unset this when no longer needed!
  let decodePositionallyEncodedOperands = 1;

  let noNamedPositionallyEncodedOperands = 1;
}

def PPCAsmParser : AsmParser {
  let ShouldEmitMatchRegisterName = 0;
}

def PPCAsmParserVariant : AsmParserVariant {
  int Variant = 0;

  // We do not use hard coded registers in asm strings.  However, some
  // InstAlias definitions use immediate literals.  Set RegisterPrefix
  // so that those are not misinterpreted as registers.
  string RegisterPrefix = "%";
  string BreakCharacters = ".";
}

def PPC : Target {
  // Information about the instructions.
  let InstructionSet = PPCInstrInfo;

  let AssemblyParsers = [PPCAsmParser];
  let AssemblyParserVariants = [PPCAsmParserVariant];
  let AllowRegisterRenaming = 1;
}

//===----------------------------------------------------------------------===//
// Pfm Counters
//===----------------------------------------------------------------------===//

include "PPCPfmCounters.td"
