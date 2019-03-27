//===-- ARM.td - Describe the ARM Target Machine -----------*- tablegen -*-===//
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
// Target-independent interfaces which we are implementing
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

//===----------------------------------------------------------------------===//
// ARM Subtarget state.
//

def ModeThumb             : SubtargetFeature<"thumb-mode", "InThumbMode",
                                             "true", "Thumb mode">;

def ModeSoftFloat         : SubtargetFeature<"soft-float","UseSoftFloat",
                                             "true", "Use software floating "
                                             "point features.">;


//===----------------------------------------------------------------------===//
// ARM Subtarget features.
//

// Floating Point, HW Division and Neon Support
def FeatureVFP2           : SubtargetFeature<"vfp2", "HasVFPv2", "true",
                                             "Enable VFP2 instructions">;

def FeatureVFP3           : SubtargetFeature<"vfp3", "HasVFPv3", "true",
                                             "Enable VFP3 instructions",
                                             [FeatureVFP2]>;

def FeatureNEON           : SubtargetFeature<"neon", "HasNEON", "true",
                                             "Enable NEON instructions",
                                             [FeatureVFP3]>;

def FeatureFP16           : SubtargetFeature<"fp16", "HasFP16", "true",
                                             "Enable half-precision "
                                             "floating point">;

def FeatureVFP4           : SubtargetFeature<"vfp4", "HasVFPv4", "true",
                                             "Enable VFP4 instructions",
                                             [FeatureVFP3, FeatureFP16]>;

def FeatureFPARMv8        : SubtargetFeature<"fp-armv8", "HasFPARMv8",
                                             "true", "Enable ARMv8 FP",
                                             [FeatureVFP4]>;

def FeatureFullFP16       : SubtargetFeature<"fullfp16", "HasFullFP16", "true",
                                             "Enable full half-precision "
                                             "floating point",
                                             [FeatureFPARMv8]>;

def FeatureFP16FML        : SubtargetFeature<"fp16fml", "HasFP16FML", "true",
                                             "Enable full half-precision "
                                             "floating point fml instructions",
                                             [FeatureFullFP16]>;

def FeatureVFPOnlySP      : SubtargetFeature<"fp-only-sp", "FPOnlySP", "true",
                                             "Floating point unit supports "
                                             "single precision only">;

def FeatureD16            : SubtargetFeature<"d16", "HasD16", "true",
                                             "Restrict FP to 16 double registers">;

def FeatureHWDivThumb     : SubtargetFeature<"hwdiv",
                                             "HasHardwareDivideInThumb", "true",
                                             "Enable divide instructions in Thumb">;

def FeatureHWDivARM       : SubtargetFeature<"hwdiv-arm",
                                             "HasHardwareDivideInARM", "true",
                                             "Enable divide instructions in ARM mode">;

// Atomic Support
def FeatureDB             : SubtargetFeature<"db", "HasDataBarrier", "true",
                                             "Has data barrier (dmb/dsb) instructions">;

def FeatureV7Clrex        : SubtargetFeature<"v7clrex", "HasV7Clrex", "true",
                                             "Has v7 clrex instruction">;

def FeatureDFB  : SubtargetFeature<"dfb", "HasFullDataBarrier", "true",
                                   "Has full data barrier (dfb) instruction">;

def FeatureAcquireRelease : SubtargetFeature<"acquire-release",
                                             "HasAcquireRelease", "true",
                                             "Has v8 acquire/release (lda/ldaex "
                                             " etc) instructions">;


def FeatureSlowFPBrcc     : SubtargetFeature<"slow-fp-brcc", "SlowFPBrcc", "true",
                                             "FP compare + branch is slow">;

def FeaturePerfMon        : SubtargetFeature<"perfmon", "HasPerfMon", "true",
                                             "Enable support for Performance "
                                             "Monitor extensions">;


// TrustZone Security Extensions
def FeatureTrustZone      : SubtargetFeature<"trustzone", "HasTrustZone", "true",
                                             "Enable support for TrustZone "
                                             "security extensions">;

def Feature8MSecExt       : SubtargetFeature<"8msecext", "Has8MSecExt", "true",
                                             "Enable support for ARMv8-M "
                                             "Security Extensions">;

def FeatureSHA2           : SubtargetFeature<"sha2", "HasSHA2", "true",
                                             "Enable SHA1 and SHA256 support", [FeatureNEON]>;

def FeatureAES            : SubtargetFeature<"aes", "HasAES", "true",
                                             "Enable AES support", [FeatureNEON]>;

def FeatureCrypto         : SubtargetFeature<"crypto", "HasCrypto", "true",
                                             "Enable support for "
                                             "Cryptography extensions",
                                             [FeatureNEON, FeatureSHA2, FeatureAES]>;

def FeatureCRC            : SubtargetFeature<"crc", "HasCRC", "true",
                                             "Enable support for CRC instructions">;

def FeatureDotProd        : SubtargetFeature<"dotprod", "HasDotProd", "true",
                                             "Enable support for dot product instructions",
                                             [FeatureNEON]>;

// Not to be confused with FeatureHasRetAddrStack (return address stack)
def FeatureRAS            : SubtargetFeature<"ras", "HasRAS", "true",
                                             "Enable Reliability, Availability "
                                             "and Serviceability extensions">;

// Fast computation of non-negative address offsets
def FeatureFPAO           : SubtargetFeature<"fpao", "HasFPAO", "true",
                                             "Enable fast computation of "
                                             "positive address offsets">;

// Fast execution of AES crypto operations
def FeatureFuseAES        : SubtargetFeature<"fuse-aes", "HasFuseAES", "true",
                                             "CPU fuses AES crypto operations">;

// Fast execution of bottom and top halves of literal generation
def FeatureFuseLiterals   : SubtargetFeature<"fuse-literals", "HasFuseLiterals", "true",
                                             "CPU fuses literal generation operations">;

// The way of reading thread pointer                                             
def FeatureReadTp :  SubtargetFeature<"read-tp-hard", "ReadTPHard", "true",
                                      "Reading thread pointer from register">;

// Cyclone can zero VFP registers in 0 cycles.
def FeatureZCZeroing      : SubtargetFeature<"zcz", "HasZeroCycleZeroing", "true",
                                             "Has zero-cycle zeroing instructions">;

// Whether it is profitable to unpredicate certain instructions during if-conversion
def FeatureProfUnpredicate : SubtargetFeature<"prof-unpr",
                                              "IsProfitableToUnpredicate", "true",
                                              "Is profitable to unpredicate">;

// Some targets (e.g. Swift) have microcoded VGETLNi32.
def FeatureSlowVGETLNi32  : SubtargetFeature<"slow-vgetlni32",
                                             "HasSlowVGETLNi32", "true",
                                             "Has slow VGETLNi32 - prefer VMOV">;

// Some targets (e.g. Swift) have microcoded VDUP32.
def FeatureSlowVDUP32     : SubtargetFeature<"slow-vdup32", "HasSlowVDUP32",
                                             "true",
                                             "Has slow VDUP32 - prefer VMOV">;

// Some targets (e.g. Cortex-A9) prefer VMOVSR to VMOVDRR even when using NEON
// for scalar FP, as this allows more effective execution domain optimization.
def FeaturePreferVMOVSR   : SubtargetFeature<"prefer-vmovsr", "PreferVMOVSR",
                                             "true", "Prefer VMOVSR">;

// Swift has ISHST barriers compatible with Atomic Release semantics but weaker
// than ISH
def FeaturePrefISHSTBarrier : SubtargetFeature<"prefer-ishst", "PreferISHST",
                                               "true", "Prefer ISHST barriers">;

// Some targets (e.g. Cortex-A9) have muxed AGU and NEON/FPU.
def FeatureMuxedUnits     : SubtargetFeature<"muxed-units", "HasMuxedUnits",
                                             "true",
                                             "Has muxed AGU and NEON/FPU">;

// Whether VLDM/VSTM starting with odd register number need more microops
// than single VLDRS
def FeatureSlowOddRegister : SubtargetFeature<"slow-odd-reg", "SlowOddRegister",
                                              "true", "VLDM/VSTM starting "
                                              "with an odd register is slow">;

// Some targets have a renaming dependency when loading into D subregisters.
def FeatureSlowLoadDSubreg : SubtargetFeature<"slow-load-D-subreg",
                                              "SlowLoadDSubregister", "true",
                                              "Loading into D subregs is slow">;

def FeatureUseWideStrideVFP : SubtargetFeature<"wide-stride-vfp",
                                               "UseWideStrideVFP", "true",
                                               "Use a wide stride when allocating VFP registers">;

// Some targets (e.g. Cortex-A15) never want VMOVS to be widened to VMOVD.
def FeatureDontWidenVMOVS : SubtargetFeature<"dont-widen-vmovs",
                                             "DontWidenVMOVS", "true",
                                             "Don't widen VMOVS to VMOVD">;

// Some targets (e.g. Cortex-A15) prefer to avoid mixing operations on different
// VFP register widths.
def FeatureSplatVFPToNeon : SubtargetFeature<"splat-vfp-neon",
                                             "SplatVFPToNeon", "true",
                                             "Splat register from VFP to NEON",
                                             [FeatureDontWidenVMOVS]>;

// Whether or not it is profitable to expand VFP/NEON MLA/MLS instructions.
def FeatureExpandMLx      : SubtargetFeature<"expand-fp-mlx",
                                             "ExpandMLx", "true",
                                             "Expand VFP/NEON MLA/MLS instructions">;

// Some targets have special RAW hazards for VFP/NEON VMLA/VMLS.
def FeatureHasVMLxHazards : SubtargetFeature<"vmlx-hazards", "HasVMLxHazards",
                                             "true", "Has VMLx hazards">;

// Some targets (e.g. Cortex-A9) want to convert VMOVRS, VMOVSR and VMOVS from
// VFP to NEON, as an execution domain optimization.
def FeatureNEONForFPMovs  : SubtargetFeature<"neon-fpmovs",
                                             "UseNEONForFPMovs", "true",
                                             "Convert VMOVSR, VMOVRS, "
                                             "VMOVS to NEON">;

// Some processors benefit from using NEON instructions for scalar
// single-precision FP operations. This affects instruction selection and should
// only be enabled if the handling of denormals is not important.
def FeatureNEONForFP      : SubtargetFeature<"neonfp",
                                             "UseNEONForSinglePrecisionFP",
                                             "true",
                                             "Use NEON for single precision FP">;

// On some processors, VLDn instructions that access unaligned data take one
// extra cycle. Take that into account when computing operand latencies.
def FeatureCheckVLDnAlign : SubtargetFeature<"vldn-align", "CheckVLDnAlign",
                                             "true",
                                             "Check for VLDn unaligned access">;

// Some processors have a nonpipelined VFP coprocessor.
def FeatureNonpipelinedVFP : SubtargetFeature<"nonpipelined-vfp",
                                              "NonpipelinedVFP", "true",
                                              "VFP instructions are not pipelined">;

// Some processors have FP multiply-accumulate instructions that don't
// play nicely with other VFP / NEON instructions, and it's generally better
// to just not use them.
def FeatureHasSlowFPVMLx  : SubtargetFeature<"slowfpvmlx", "SlowFPVMLx", "true",
                                             "Disable VFP / NEON MAC instructions">;

// Cortex-A8 / A9 Advanced SIMD has multiplier accumulator forwarding.
def FeatureVMLxForwarding : SubtargetFeature<"vmlx-forwarding",
                                             "HasVMLxForwarding", "true",
                                             "Has multiplier accumulator forwarding">;

// Disable 32-bit to 16-bit narrowing for experimentation.
def FeaturePref32BitThumb : SubtargetFeature<"32bit", "Pref32BitThumb", "true",
                                             "Prefer 32-bit Thumb instrs">;

def FeaturePrefLoopAlign32 : SubtargetFeature<"loop-align", "PrefLoopAlignment","2",
                                              "Prefer 32-bit alignment for loops">;

/// Some instructions update CPSR partially, which can add false dependency for
/// out-of-order implementation, e.g. Cortex-A9, unless each individual bit is
/// mapped to a separate physical register. Avoid partial CPSR update for these
/// processors.
def FeatureAvoidPartialCPSR : SubtargetFeature<"avoid-partial-cpsr",
                                               "AvoidCPSRPartialUpdate", "true",
                                 "Avoid CPSR partial update for OOO execution">;

/// Disable +1 predication cost for instructions updating CPSR.
/// Enabled for Cortex-A57.
def FeatureCheapPredicableCPSR : SubtargetFeature<"cheap-predicable-cpsr",
                                                  "CheapPredicableCPSRDef",
                                                  "true",
                  "Disable +1 predication cost for instructions updating CPSR">;

def FeatureAvoidMOVsShOp  : SubtargetFeature<"avoid-movs-shop",
                                             "AvoidMOVsShifterOperand", "true",
                                             "Avoid movs instructions with "
                                             "shifter operand">;

// Some processors perform return stack prediction. CodeGen should avoid issue
// "normal" call instructions to callees which do not return.
def FeatureHasRetAddrStack : SubtargetFeature<"ret-addr-stack",
                                              "HasRetAddrStack", "true",
                                              "Has return address stack">;

// Some processors have no branch predictor, which changes the expected cost of
// taking a branch which affects the choice of whether to use predicated
// instructions.
def FeatureHasNoBranchPredictor : SubtargetFeature<"no-branch-predictor",
                                                   "HasBranchPredictor", "false",
                                                   "Has no branch predictor">;

/// DSP extension.
def FeatureDSP            : SubtargetFeature<"dsp", "HasDSP", "true",
                                             "Supports DSP instructions in "
                                             "ARM and/or Thumb2">;

// Multiprocessing extension.
def FeatureMP             : SubtargetFeature<"mp", "HasMPExtension", "true",
                                        "Supports Multiprocessing extension">;

// Virtualization extension - requires HW divide (ARMv7-AR ARMARM - 4.4.8).
def FeatureVirtualization : SubtargetFeature<"virtualization",
                                             "HasVirtualization", "true",
                                             "Supports Virtualization extension",
                                             [FeatureHWDivThumb, FeatureHWDivARM]>;

// Special TRAP encoding for NaCl, which looks like a TRAP in Thumb too.
// See ARMInstrInfo.td for details.
def FeatureNaClTrap       : SubtargetFeature<"nacl-trap", "UseNaClTrap", "true",
                                             "NaCl trap">;

def FeatureStrictAlign    : SubtargetFeature<"strict-align",
                                             "StrictAlign", "true",
                                             "Disallow all unaligned memory "
                                             "access">;

def FeatureLongCalls      : SubtargetFeature<"long-calls", "GenLongCalls", "true",
                                             "Generate calls via indirect call "
                                             "instructions">;

def FeatureExecuteOnly    : SubtargetFeature<"execute-only",
                                             "GenExecuteOnly", "true",
                                             "Enable the generation of "
                                             "execute only code.">;

def FeatureReserveR9      : SubtargetFeature<"reserve-r9", "ReserveR9", "true",
                                             "Reserve R9, making it unavailable"
                                             " as GPR">;

def FeatureNoMovt         : SubtargetFeature<"no-movt", "NoMovt", "true",
                                             "Don't use movt/movw pairs for "
                                             "32-bit imms">;

def FeatureNoNegativeImmediates
                          : SubtargetFeature<"no-neg-immediates",
                                             "NegativeImmediates", "false",
                                             "Convert immediates and instructions "
                                             "to their negated or complemented "
                                             "equivalent when the immediate does "
                                             "not fit in the encoding.">;

// Use the MachineScheduler for instruction scheduling for the subtarget.
def FeatureUseMISched: SubtargetFeature<"use-misched", "UseMISched", "true",
                                        "Use the MachineScheduler">;

def FeatureNoPostRASched : SubtargetFeature<"disable-postra-scheduler",
    "DisablePostRAScheduler", "true",
    "Don't schedule again after register allocation">;

// Enable use of alias analysis during code generation
def FeatureUseAA : SubtargetFeature<"use-aa", "UseAA", "true",
                                    "Use alias analysis during codegen">;

// Armv8.5-A extensions

def FeatureSB       : SubtargetFeature<"sb", "HasSB", "true",
  "Enable v8.5a Speculation Barrier" >;

//===----------------------------------------------------------------------===//
// ARM architecture class
//

// A-series ISA
def FeatureAClass : SubtargetFeature<"aclass", "ARMProcClass", "AClass",
                                     "Is application profile ('A' series)">;

// R-series ISA
def FeatureRClass : SubtargetFeature<"rclass", "ARMProcClass", "RClass",
                                     "Is realtime profile ('R' series)">;

// M-series ISA
def FeatureMClass : SubtargetFeature<"mclass", "ARMProcClass", "MClass",
                                     "Is microcontroller profile ('M' series)">;


def FeatureThumb2 : SubtargetFeature<"thumb2", "HasThumb2", "true",
                                     "Enable Thumb2 instructions">;

def FeatureNoARM  : SubtargetFeature<"noarm", "NoARM", "true",
                                     "Does not support ARM mode execution">;

//===----------------------------------------------------------------------===//
// ARM ISAa.
//

def HasV4TOps   : SubtargetFeature<"v4t", "HasV4TOps", "true",
                                   "Support ARM v4T instructions">;

def HasV5TOps   : SubtargetFeature<"v5t", "HasV5TOps", "true",
                                   "Support ARM v5T instructions",
                                   [HasV4TOps]>;

def HasV5TEOps  : SubtargetFeature<"v5te", "HasV5TEOps", "true",
                                   "Support ARM v5TE, v5TEj, and "
                                   "v5TExp instructions",
                                   [HasV5TOps]>;

def HasV6Ops    : SubtargetFeature<"v6", "HasV6Ops", "true",
                                   "Support ARM v6 instructions",
                                   [HasV5TEOps]>;

def HasV6MOps   : SubtargetFeature<"v6m", "HasV6MOps", "true",
                                   "Support ARM v6M instructions",
                                   [HasV6Ops]>;

def HasV8MBaselineOps : SubtargetFeature<"v8m", "HasV8MBaselineOps", "true",
                                         "Support ARM v8M Baseline instructions",
                                         [HasV6MOps]>;

def HasV6KOps   : SubtargetFeature<"v6k", "HasV6KOps", "true",
                                   "Support ARM v6k instructions",
                                   [HasV6Ops]>;

def HasV6T2Ops  : SubtargetFeature<"v6t2", "HasV6T2Ops", "true",
                                   "Support ARM v6t2 instructions",
                                   [HasV8MBaselineOps, HasV6KOps, FeatureThumb2]>;

def HasV7Ops    : SubtargetFeature<"v7", "HasV7Ops", "true",
                                   "Support ARM v7 instructions",
                                   [HasV6T2Ops, FeaturePerfMon,
                                    FeatureV7Clrex]>;

def HasV8MMainlineOps :
                  SubtargetFeature<"v8m.main", "HasV8MMainlineOps", "true",
                                   "Support ARM v8M Mainline instructions",
                                   [HasV7Ops]>;

def HasV8Ops    : SubtargetFeature<"v8", "HasV8Ops", "true",
                                   "Support ARM v8 instructions",
                                   [HasV7Ops, FeatureAcquireRelease]>;

def HasV8_1aOps : SubtargetFeature<"v8.1a", "HasV8_1aOps", "true",
                                   "Support ARM v8.1a instructions",
                                   [HasV8Ops]>;

def HasV8_2aOps : SubtargetFeature<"v8.2a", "HasV8_2aOps", "true",
                                   "Support ARM v8.2a instructions",
                                   [HasV8_1aOps]>;

def HasV8_3aOps   : SubtargetFeature<"v8.3a", "HasV8_3aOps", "true",
                                   "Support ARM v8.3a instructions",
                                   [HasV8_2aOps]>;

def HasV8_4aOps   : SubtargetFeature<"v8.4a", "HasV8_4aOps", "true",
                                   "Support ARM v8.4a instructions",
                                   [HasV8_3aOps, FeatureDotProd]>;

def HasV8_5aOps   : SubtargetFeature<"v8.5a", "HasV8_5aOps", "true",
                                   "Support ARM v8.5a instructions",
                                   [HasV8_4aOps, FeatureSB]>;

//===----------------------------------------------------------------------===//
// ARM Processor subtarget features.
//

def ProcA5      : SubtargetFeature<"a5", "ARMProcFamily", "CortexA5",
                                   "Cortex-A5 ARM processors", []>;
def ProcA7      : SubtargetFeature<"a7", "ARMProcFamily", "CortexA7",
                                   "Cortex-A7 ARM processors", []>;
def ProcA8      : SubtargetFeature<"a8", "ARMProcFamily", "CortexA8",
                                   "Cortex-A8 ARM processors", []>;
def ProcA9      : SubtargetFeature<"a9", "ARMProcFamily", "CortexA9",
                                   "Cortex-A9 ARM processors", []>;
def ProcA12     : SubtargetFeature<"a12", "ARMProcFamily", "CortexA12",
                                   "Cortex-A12 ARM processors", []>;
def ProcA15     : SubtargetFeature<"a15", "ARMProcFamily", "CortexA15",
                                   "Cortex-A15 ARM processors", []>;
def ProcA17     : SubtargetFeature<"a17", "ARMProcFamily", "CortexA17",
                                   "Cortex-A17 ARM processors", []>;
def ProcA32     : SubtargetFeature<"a32", "ARMProcFamily", "CortexA32",
                                   "Cortex-A32 ARM processors", []>;
def ProcA35     : SubtargetFeature<"a35", "ARMProcFamily", "CortexA35",
                                   "Cortex-A35 ARM processors", []>;
def ProcA53     : SubtargetFeature<"a53", "ARMProcFamily", "CortexA53",
                                   "Cortex-A53 ARM processors", []>;
def ProcA55     : SubtargetFeature<"a55", "ARMProcFamily", "CortexA55",
                                   "Cortex-A55 ARM processors", []>;
def ProcA57     : SubtargetFeature<"a57", "ARMProcFamily", "CortexA57",
                                   "Cortex-A57 ARM processors", []>;
def ProcA72     : SubtargetFeature<"a72", "ARMProcFamily", "CortexA72",
                                   "Cortex-A72 ARM processors", []>;
def ProcA73     : SubtargetFeature<"a73", "ARMProcFamily", "CortexA73",
                                   "Cortex-A73 ARM processors", []>;
def ProcA75     : SubtargetFeature<"a75", "ARMProcFamily", "CortexA75",
                                   "Cortex-A75 ARM processors", []>;

def ProcKrait   : SubtargetFeature<"krait", "ARMProcFamily", "Krait",
                                   "Qualcomm Krait processors", []>;
def ProcKryo    : SubtargetFeature<"kryo", "ARMProcFamily", "Kryo",
                                   "Qualcomm Kryo processors", []>;
def ProcSwift   : SubtargetFeature<"swift", "ARMProcFamily", "Swift",
                                   "Swift ARM processors", []>;

def ProcExynos  : SubtargetFeature<"exynos", "ARMProcFamily", "Exynos",
                                   "Samsung Exynos processors",
                                   [FeatureZCZeroing,
                                    FeatureUseWideStrideVFP,
                                    FeatureUseAA,
                                    FeatureSplatVFPToNeon,
                                    FeatureSlowVGETLNi32,
                                    FeatureSlowVDUP32,
                                    FeatureSlowFPBrcc,
                                    FeatureProfUnpredicate,
                                    FeatureHWDivThumb,
                                    FeatureHWDivARM,
                                    FeatureHasSlowFPVMLx,
                                    FeatureHasRetAddrStack,
                                    FeatureFuseLiterals,
                                    FeatureFuseAES,
                                    FeatureExpandMLx,
                                    FeatureCrypto,
                                    FeatureCRC]>;

def ProcR4      : SubtargetFeature<"r4", "ARMProcFamily", "CortexR4",
                                   "Cortex-R4 ARM processors", []>;
def ProcR5      : SubtargetFeature<"r5", "ARMProcFamily", "CortexR5",
                                   "Cortex-R5 ARM processors", []>;
def ProcR7      : SubtargetFeature<"r7", "ARMProcFamily", "CortexR7",
                                   "Cortex-R7 ARM processors", []>;
def ProcR52     : SubtargetFeature<"r52", "ARMProcFamily", "CortexR52",
                                   "Cortex-R52 ARM processors", []>;

def ProcM3      : SubtargetFeature<"m3", "ARMProcFamily", "CortexM3",
                                   "Cortex-M3 ARM processors", []>;


//===----------------------------------------------------------------------===//
// ARM Helper classes.
//

class Architecture<string fname, string aname, list<SubtargetFeature> features>
  : SubtargetFeature<fname, "ARMArch", aname,
                     !strconcat(aname, " architecture"), features>;

class ProcNoItin<string Name, list<SubtargetFeature> Features>
  : Processor<Name, NoItineraries, Features>;


//===----------------------------------------------------------------------===//
// ARM architectures
//

def ARMv2     : Architecture<"armv2",     "ARMv2",    []>;

def ARMv2a    : Architecture<"armv2a",    "ARMv2a",   []>;

def ARMv3     : Architecture<"armv3",     "ARMv3",    []>;

def ARMv3m    : Architecture<"armv3m",    "ARMv3m",   []>;

def ARMv4     : Architecture<"armv4",     "ARMv4",    []>;

def ARMv4t    : Architecture<"armv4t",    "ARMv4t",   [HasV4TOps]>;

def ARMv5t    : Architecture<"armv5t",    "ARMv5t",   [HasV5TOps]>;

def ARMv5te   : Architecture<"armv5te",   "ARMv5te",  [HasV5TEOps]>;

def ARMv5tej  : Architecture<"armv5tej",  "ARMv5tej", [HasV5TEOps]>;

def ARMv6     : Architecture<"armv6",     "ARMv6",    [HasV6Ops,
                                                       FeatureDSP]>;

def ARMv6t2   : Architecture<"armv6t2",   "ARMv6t2",  [HasV6T2Ops,
                                                       FeatureDSP]>;

def ARMv6k    : Architecture<"armv6k",    "ARMv6k",   [HasV6KOps]>;

def ARMv6kz   : Architecture<"armv6kz",   "ARMv6kz",  [HasV6KOps,
                                                       FeatureTrustZone]>;

def ARMv6m    : Architecture<"armv6-m",   "ARMv6m",   [HasV6MOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureMClass,
                                                       FeatureStrictAlign]>;

def ARMv6sm   : Architecture<"armv6s-m",  "ARMv6sm",  [HasV6MOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureMClass,
                                                       FeatureStrictAlign]>;

def ARMv7a    : Architecture<"armv7-a",   "ARMv7a",   [HasV7Ops,
                                                       FeatureNEON,
                                                       FeatureDB,
                                                       FeatureDSP,
                                                       FeatureAClass]>;

def ARMv7ve   : Architecture<"armv7ve",   "ARMv7ve",  [HasV7Ops,
                                                       FeatureNEON,
                                                       FeatureDB,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureAClass]>;

def ARMv7r    : Architecture<"armv7-r",   "ARMv7r",   [HasV7Ops,
                                                       FeatureDB,
                                                       FeatureDSP,
                                                       FeatureHWDivThumb,
                                                       FeatureRClass]>;

def ARMv7m    : Architecture<"armv7-m",   "ARMv7m",   [HasV7Ops,
                                                       FeatureThumb2,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       FeatureMClass]>;

def ARMv7em   : Architecture<"armv7e-m",  "ARMv7em",  [HasV7Ops,
                                                       FeatureThumb2,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       FeatureMClass,
                                                       FeatureDSP]>;

def ARMv8a    : Architecture<"armv8-a",   "ARMv8a",   [HasV8Ops,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC]>;

def ARMv81a   : Architecture<"armv8.1-a", "ARMv81a",  [HasV8_1aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC]>;

def ARMv82a   : Architecture<"armv8.2-a", "ARMv82a",  [HasV8_2aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS]>;

def ARMv83a   : Architecture<"armv8.3-a", "ARMv83a",  [HasV8_3aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS]>;

def ARMv84a   : Architecture<"armv8.4-a", "ARMv84a",  [HasV8_4aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;

def ARMv85a   : Architecture<"armv8.5-a", "ARMv85a",  [HasV8_5aOps,
                                                       FeatureAClass,
                                                       FeatureDB,
                                                       FeatureFPARMv8,
                                                       FeatureNEON,
                                                       FeatureDSP,
                                                       FeatureTrustZone,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureCrypto,
                                                       FeatureCRC,
                                                       FeatureRAS,
                                                       FeatureDotProd]>;

def ARMv8r    : Architecture<"armv8-r",   "ARMv8r",   [HasV8Ops,
                                                       FeatureRClass,
                                                       FeatureDB,
                                                       FeatureDFB,
                                                       FeatureDSP,
                                                       FeatureCRC,
                                                       FeatureMP,
                                                       FeatureVirtualization,
                                                       FeatureFPARMv8,
                                                       FeatureNEON]>;

def ARMv8mBaseline : Architecture<"armv8-m.base", "ARMv8mBaseline",
                                                      [HasV8MBaselineOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       FeatureV7Clrex,
                                                       Feature8MSecExt,
                                                       FeatureAcquireRelease,
                                                       FeatureMClass,
                                                       FeatureStrictAlign]>;

def ARMv8mMainline : Architecture<"armv8-m.main", "ARMv8mMainline",
                                                      [HasV8MMainlineOps,
                                                       FeatureNoARM,
                                                       ModeThumb,
                                                       FeatureDB,
                                                       FeatureHWDivThumb,
                                                       Feature8MSecExt,
                                                       FeatureAcquireRelease,
                                                       FeatureMClass]>;

// Aliases
def IWMMXT   : Architecture<"iwmmxt",      "ARMv5te",  [ARMv5te]>;
def IWMMXT2  : Architecture<"iwmmxt2",     "ARMv5te",  [ARMv5te]>;
def XScale   : Architecture<"xscale",      "ARMv5te",  [ARMv5te]>;
def ARMv6j   : Architecture<"armv6j",      "ARMv7a",   [ARMv6]>;
def ARMv7k   : Architecture<"armv7k",      "ARMv7a",   [ARMv7a]>;
def ARMv7s   : Architecture<"armv7s",      "ARMv7a",   [ARMv7a]>;


//===----------------------------------------------------------------------===//
// ARM schedules.
//===----------------------------------------------------------------------===//
//
include "ARMSchedule.td"

//===----------------------------------------------------------------------===//
// ARM processors
//

// Dummy CPU, used to target architectures
def : ProcessorModel<"generic",     CortexA8Model,      []>;

// FIXME: Several processors below are not using their own scheduler
// model, but one of similar/previous processor. These should be fixed.

def : ProcNoItin<"arm8",                                [ARMv4]>;
def : ProcNoItin<"arm810",                              [ARMv4]>;
def : ProcNoItin<"strongarm",                           [ARMv4]>;
def : ProcNoItin<"strongarm110",                        [ARMv4]>;
def : ProcNoItin<"strongarm1100",                       [ARMv4]>;
def : ProcNoItin<"strongarm1110",                       [ARMv4]>;

def : ProcNoItin<"arm7tdmi",                            [ARMv4t]>;
def : ProcNoItin<"arm7tdmi-s",                          [ARMv4t]>;
def : ProcNoItin<"arm710t",                             [ARMv4t]>;
def : ProcNoItin<"arm720t",                             [ARMv4t]>;
def : ProcNoItin<"arm9",                                [ARMv4t]>;
def : ProcNoItin<"arm9tdmi",                            [ARMv4t]>;
def : ProcNoItin<"arm920",                              [ARMv4t]>;
def : ProcNoItin<"arm920t",                             [ARMv4t]>;
def : ProcNoItin<"arm922t",                             [ARMv4t]>;
def : ProcNoItin<"arm940t",                             [ARMv4t]>;
def : ProcNoItin<"ep9312",                              [ARMv4t]>;

def : ProcNoItin<"arm10tdmi",                           [ARMv5t]>;
def : ProcNoItin<"arm1020t",                            [ARMv5t]>;

def : ProcNoItin<"arm9e",                               [ARMv5te]>;
def : ProcNoItin<"arm926ej-s",                          [ARMv5te]>;
def : ProcNoItin<"arm946e-s",                           [ARMv5te]>;
def : ProcNoItin<"arm966e-s",                           [ARMv5te]>;
def : ProcNoItin<"arm968e-s",                           [ARMv5te]>;
def : ProcNoItin<"arm10e",                              [ARMv5te]>;
def : ProcNoItin<"arm1020e",                            [ARMv5te]>;
def : ProcNoItin<"arm1022e",                            [ARMv5te]>;
def : ProcNoItin<"xscale",                              [ARMv5te]>;
def : ProcNoItin<"iwmmxt",                              [ARMv5te]>;

def : Processor<"arm1136j-s",       ARMV6Itineraries,   [ARMv6]>;
def : Processor<"arm1136jf-s",      ARMV6Itineraries,   [ARMv6,
                                                         FeatureVFP2,
                                                         FeatureHasSlowFPVMLx]>;

def : Processor<"cortex-m0",        ARMV6Itineraries,   [ARMv6m]>;
def : Processor<"cortex-m0plus",    ARMV6Itineraries,   [ARMv6m]>;
def : Processor<"cortex-m1",        ARMV6Itineraries,   [ARMv6m]>;
def : Processor<"sc000",            ARMV6Itineraries,   [ARMv6m]>;

def : Processor<"arm1176j-s",       ARMV6Itineraries,   [ARMv6kz]>;
def : Processor<"arm1176jz-s",      ARMV6Itineraries,   [ARMv6kz]>;
def : Processor<"arm1176jzf-s",     ARMV6Itineraries,   [ARMv6kz,
                                                         FeatureVFP2,
                                                         FeatureHasSlowFPVMLx]>;

def : Processor<"mpcorenovfp",      ARMV6Itineraries,   [ARMv6k]>;
def : Processor<"mpcore",           ARMV6Itineraries,   [ARMv6k,
                                                         FeatureVFP2,
                                                         FeatureHasSlowFPVMLx]>;

def : Processor<"arm1156t2-s",      ARMV6Itineraries,   [ARMv6t2]>;
def : Processor<"arm1156t2f-s",     ARMV6Itineraries,   [ARMv6t2,
                                                         FeatureVFP2,
                                                         FeatureHasSlowFPVMLx]>;

def : ProcessorModel<"cortex-a5",   CortexA8Model,      [ARMv7a, ProcA5,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureVMLxForwarding,
                                                         FeatureMP,
                                                         FeatureVFP4]>;

def : ProcessorModel<"cortex-a7",   CortexA8Model,      [ARMv7a, ProcA7,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHasVMLxHazards,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureVMLxForwarding,
                                                         FeatureMP,
                                                         FeatureVFP4,
                                                         FeatureVirtualization]>;

def : ProcessorModel<"cortex-a8",   CortexA8Model,      [ARMv7a, ProcA8,
                                                         FeatureHasRetAddrStack,
                                                         FeatureNonpipelinedVFP,
                                                         FeatureTrustZone,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHasVMLxHazards,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureVMLxForwarding]>;

def : ProcessorModel<"cortex-a9",   CortexA9Model,      [ARMv7a, ProcA9,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureHasVMLxHazards,
                                                         FeatureVMLxForwarding,
                                                         FeatureFP16,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureExpandMLx,
                                                         FeaturePreferVMOVSR,
                                                         FeatureMuxedUnits,
                                                         FeatureNEONForFPMovs,
                                                         FeatureCheckVLDnAlign,
                                                         FeatureMP]>;

def : ProcessorModel<"cortex-a12",  CortexA9Model,      [ARMv7a, ProcA12,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureVMLxForwarding,
                                                         FeatureVFP4,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureVirtualization,
                                                         FeatureMP]>;

def : ProcessorModel<"cortex-a15",  CortexA9Model,      [ARMv7a, ProcA15,
                                                         FeatureDontWidenVMOVS,
                                                         FeatureSplatVFPToNeon,
                                                         FeatureHasRetAddrStack,
                                                         FeatureMuxedUnits,
                                                         FeatureTrustZone,
                                                         FeatureVFP4,
                                                         FeatureMP,
                                                         FeatureCheckVLDnAlign,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureVirtualization]>;

def : ProcessorModel<"cortex-a17",  CortexA9Model,      [ARMv7a, ProcA17,
                                                         FeatureHasRetAddrStack,
                                                         FeatureTrustZone,
                                                         FeatureMP,
                                                         FeatureVMLxForwarding,
                                                         FeatureVFP4,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureVirtualization]>;

// FIXME: krait has currently the same features as A9 plus VFP4 and  HWDiv
def : ProcessorModel<"krait",       CortexA9Model,      [ARMv7a, ProcKrait,
                                                         FeatureHasRetAddrStack,
                                                         FeatureMuxedUnits,
                                                         FeatureCheckVLDnAlign,
                                                         FeatureVMLxForwarding,
                                                         FeatureFP16,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureVFP4,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM]>;

def : ProcessorModel<"swift",       SwiftModel,         [ARMv7a, ProcSwift,
                                                         FeatureHasRetAddrStack,
                                                         FeatureNEONForFP,
                                                         FeatureVFP4,
                                                         FeatureUseWideStrideVFP,
                                                         FeatureMP,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureAvoidMOVsShOp,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasVMLxHazards,
                                                         FeatureProfUnpredicate,
                                                         FeaturePrefISHSTBarrier,
                                                         FeatureSlowOddRegister,
                                                         FeatureSlowLoadDSubreg,
                                                         FeatureSlowVGETLNi32,
                                                         FeatureSlowVDUP32,
                                                         FeatureUseMISched,
                                                         FeatureNoPostRASched]>;

def : ProcessorModel<"cortex-r4",   CortexA8Model,      [ARMv7r, ProcR4,
                                                         FeatureHasRetAddrStack,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-r4f",  CortexA8Model,      [ARMv7r, ProcR4,
                                                         FeatureHasRetAddrStack,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureVFP3,
                                                         FeatureD16,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-r5",   CortexA8Model,      [ARMv7r, ProcR5,
                                                         FeatureHasRetAddrStack,
                                                         FeatureVFP3,
                                                         FeatureD16,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHWDivARM,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-r7",   CortexA8Model,      [ARMv7r, ProcR7,
                                                         FeatureHasRetAddrStack,
                                                         FeatureVFP3,
                                                         FeatureD16,
                                                         FeatureFP16,
                                                         FeatureMP,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHWDivARM,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-r8",   CortexA8Model,      [ARMv7r,
                                                         FeatureHasRetAddrStack,
                                                         FeatureVFP3,
                                                         FeatureD16,
                                                         FeatureFP16,
                                                         FeatureMP,
                                                         FeatureSlowFPBrcc,
                                                         FeatureHWDivARM,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureAvoidPartialCPSR]>;

def : ProcessorModel<"cortex-m3", CortexM3Model,        [ARMv7m,
                                                         ProcM3,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureHasNoBranchPredictor]>;

def : ProcessorModel<"sc300",     CortexM3Model,        [ARMv7m,
                                                         ProcM3,
                                                         FeatureHasNoBranchPredictor]>;

def : ProcessorModel<"cortex-m4", CortexM3Model,        [ARMv7em,
                                                         FeatureVFP4,
                                                         FeatureVFPOnlySP,
                                                         FeatureD16,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasNoBranchPredictor]>;

def : ProcNoItin<"cortex-m7",                           [ARMv7em,
                                                         FeatureFPARMv8,
                                                         FeatureD16]>;

def : ProcNoItin<"cortex-m23",                          [ARMv8mBaseline,
                                                         FeatureNoMovt]>;

def : ProcessorModel<"cortex-m33", CortexM3Model,       [ARMv8mMainline,
                                                         FeatureDSP,
                                                         FeatureFPARMv8,
                                                         FeatureD16,
                                                         FeatureVFPOnlySP,
                                                         FeaturePrefLoopAlign32,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureHasNoBranchPredictor]>;

def : ProcNoItin<"cortex-a32",                           [ARMv8a,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcNoItin<"cortex-a35",                          [ARMv8a, ProcA35,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcNoItin<"cortex-a53",                          [ARMv8a, ProcA53,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFPAO]>;

def : ProcNoItin<"cortex-a55",                          [ARMv82a, ProcA55,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureDotProd]>;

def : ProcessorModel<"cortex-a57",  CortexA57Model,     [ARMv8a, ProcA57,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC,
                                                         FeatureFPAO,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureCheapPredicableCPSR]>;

def : ProcessorModel<"cortex-a72",  CortexA57Model,     [ARMv8a, ProcA72,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcNoItin<"cortex-a73",                          [ARMv8a, ProcA73,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcNoItin<"cortex-a75",                          [ARMv82a, ProcA75,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureDotProd]>;

def : ProcessorModel<"cyclone",     SwiftModel,         [ARMv8a, ProcSwift,
                                                         FeatureHasRetAddrStack,
                                                         FeatureNEONForFP,
                                                         FeatureVFP4,
                                                         FeatureMP,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureAvoidPartialCPSR,
                                                         FeatureAvoidMOVsShOp,
                                                         FeatureHasSlowFPVMLx,
                                                         FeatureCrypto,
                                                         FeatureUseMISched,
                                                         FeatureZCZeroing,
                                                         FeatureNoPostRASched]>;

def : ProcNoItin<"exynos-m1",                           [ARMv8a, ProcExynos]>;
def : ProcNoItin<"exynos-m2",                           [ARMv8a, ProcExynos]>;
def : ProcNoItin<"exynos-m3",                           [ARMv8a, ProcExynos]>;
def : ProcNoItin<"exynos-m4",                           [ARMv82a, ProcExynos,
                                                         FeatureFullFP16,
                                                         FeatureDotProd]>;

def : ProcNoItin<"kryo",                                [ARMv8a, ProcKryo,
                                                         FeatureHWDivThumb,
                                                         FeatureHWDivARM,
                                                         FeatureCrypto,
                                                         FeatureCRC]>;

def : ProcessorModel<"cortex-r52", CortexR52Model,      [ARMv8r, ProcR52,
                                                         FeatureUseMISched,
                                                         FeatureFPAO,
                                                         FeatureUseAA]>;

//===----------------------------------------------------------------------===//
// Register File Description
//===----------------------------------------------------------------------===//

include "ARMRegisterInfo.td"
include "ARMRegisterBanks.td"
include "ARMCallingConv.td"

//===----------------------------------------------------------------------===//
// Instruction Descriptions
//===----------------------------------------------------------------------===//

include "ARMInstrInfo.td"
def ARMInstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// Declare the target which we are implementing
//===----------------------------------------------------------------------===//

def ARMAsmWriter : AsmWriter {
  string AsmWriterClassName  = "InstPrinter";
  int PassSubtarget = 1;
  int Variant = 0;
  bit isMCAsmWriter = 1;
}

def ARMAsmParser : AsmParser {
  bit ReportMultipleNearMisses = 1;
}

def ARMAsmParserVariant : AsmParserVariant {
  int Variant = 0;
  string Name = "ARM";
  string BreakCharacters = ".";
}

def ARM : Target {
  // Pull in Instruction Info.
  let InstructionSet = ARMInstrInfo;
  let AssemblyWriters = [ARMAsmWriter];
  let AssemblyParsers = [ARMAsmParser];
  let AssemblyParserVariants = [ARMAsmParserVariant];
  let AllowRegisterRenaming = 1;
}
