//===-- AMDGPU.td - AMDGPU Tablegen files --------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===------------------------------------------------------------===//

include "llvm/TableGen/SearchableTable.td"
include "llvm/Target/Target.td"
include "AMDGPUFeatures.td"

class BoolToList<bit Value> {
  list<int> ret = !if(Value, [1]<int>, []<int>);
}

//===------------------------------------------------------------===//
// Subtarget Features (device properties)
//===------------------------------------------------------------===//

def FeatureFastFMAF32 : SubtargetFeature<"fast-fmaf",
  "FastFMAF32",
  "true",
  "Assuming f32 fma is at least as fast as mul + add"
>;

def FeatureMIMG_R128 : SubtargetFeature<"mimg-r128",
  "MIMG_R128",
  "true",
  "Support 128-bit texture resources"
>;

def HalfRate64Ops : SubtargetFeature<"half-rate-64-ops",
  "HalfRate64Ops",
  "true",
  "Most fp64 instructions are half rate instead of quarter"
>;

def FeatureFlatAddressSpace : SubtargetFeature<"flat-address-space",
  "FlatAddressSpace",
  "true",
  "Support flat address space"
>;

def FeatureFlatInstOffsets : SubtargetFeature<"flat-inst-offsets",
  "FlatInstOffsets",
  "true",
  "Flat instructions have immediate offset addressing mode"
>;

def FeatureFlatGlobalInsts : SubtargetFeature<"flat-global-insts",
  "FlatGlobalInsts",
  "true",
  "Have global_* flat memory instructions"
>;

def FeatureFlatScratchInsts : SubtargetFeature<"flat-scratch-insts",
  "FlatScratchInsts",
  "true",
  "Have scratch_* flat memory instructions"
>;

def FeatureAddNoCarryInsts : SubtargetFeature<"add-no-carry-insts",
  "AddNoCarryInsts",
  "true",
  "Have VALU add/sub instructions without carry out"
>;

def FeatureUnalignedBufferAccess : SubtargetFeature<"unaligned-buffer-access",
  "UnalignedBufferAccess",
  "true",
  "Support unaligned global loads and stores"
>;

def FeatureTrapHandler: SubtargetFeature<"trap-handler",
  "TrapHandler",
  "true",
  "Trap handler support"
>;

def FeatureUnalignedScratchAccess : SubtargetFeature<"unaligned-scratch-access",
  "UnalignedScratchAccess",
  "true",
  "Support unaligned scratch loads and stores"
>;

def FeatureApertureRegs : SubtargetFeature<"aperture-regs",
  "HasApertureRegs",
  "true",
  "Has Memory Aperture Base and Size Registers"
>;

def FeatureMadMixInsts : SubtargetFeature<"mad-mix-insts",
  "HasMadMixInsts",
  "true",
  "Has v_mad_mix_f32, v_mad_mixlo_f16, v_mad_mixhi_f16 instructions"
>;

def FeatureFmaMixInsts : SubtargetFeature<"fma-mix-insts",
  "HasFmaMixInsts",
  "true",
  "Has v_fma_mix_f32, v_fma_mixlo_f16, v_fma_mixhi_f16 instructions"
>;

// XNACK is disabled if SH_MEM_CONFIG.ADDRESS_MODE = GPUVM on chips that support
// XNACK. The current default kernel driver setting is:
// - graphics ring: XNACK disabled
// - compute ring: XNACK enabled
//
// If XNACK is enabled, the VMEM latency can be worse.
// If XNACK is disabled, the 2 SGPRs can be used for general purposes.
def FeatureXNACK : SubtargetFeature<"xnack",
  "EnableXNACK",
  "true",
  "Enable XNACK support"
>;

def FeatureSGPRInitBug : SubtargetFeature<"sgpr-init-bug",
  "SGPRInitBug",
  "true",
  "VI SGPR initialization bug requiring a fixed SGPR allocation size"
>;

class SubtargetFeatureLDSBankCount <int Value> : SubtargetFeature <
  "ldsbankcount"#Value,
  "LDSBankCount",
  !cast<string>(Value),
  "The number of LDS banks per compute unit."
>;

def FeatureLDSBankCount16 : SubtargetFeatureLDSBankCount<16>;
def FeatureLDSBankCount32 : SubtargetFeatureLDSBankCount<32>;

def FeatureGCN3Encoding : SubtargetFeature<"gcn3-encoding",
  "GCN3Encoding",
  "true",
  "Encoding format for VI"
>;

def FeatureCIInsts : SubtargetFeature<"ci-insts",
  "CIInsts",
  "true",
  "Additional instructions for CI+"
>;

def FeatureVIInsts : SubtargetFeature<"vi-insts",
  "VIInsts",
  "true",
  "Additional instructions for VI+"
>;

def FeatureGFX9Insts : SubtargetFeature<"gfx9-insts",
  "GFX9Insts",
  "true",
  "Additional instructions for GFX9+"
>;

def FeatureSMemRealTime : SubtargetFeature<"s-memrealtime",
  "HasSMemRealTime",
  "true",
  "Has s_memrealtime instruction"
>;

def FeatureInv2PiInlineImm : SubtargetFeature<"inv-2pi-inline-imm",
  "HasInv2PiInlineImm",
  "true",
  "Has 1 / (2 * pi) as inline immediate"
>;

def Feature16BitInsts : SubtargetFeature<"16-bit-insts",
  "Has16BitInsts",
  "true",
  "Has i16/f16 instructions"
>;

def FeatureVOP3P : SubtargetFeature<"vop3p",
  "HasVOP3PInsts",
  "true",
  "Has VOP3P packed instructions"
>;

def FeatureMovrel : SubtargetFeature<"movrel",
  "HasMovrel",
  "true",
  "Has v_movrel*_b32 instructions"
>;

def FeatureVGPRIndexMode : SubtargetFeature<"vgpr-index-mode",
  "HasVGPRIndexMode",
  "true",
  "Has VGPR mode register indexing"
>;

def FeatureScalarStores : SubtargetFeature<"scalar-stores",
  "HasScalarStores",
  "true",
  "Has store scalar memory instructions"
>;

def FeatureScalarAtomics : SubtargetFeature<"scalar-atomics",
  "HasScalarAtomics",
  "true",
  "Has atomic scalar memory instructions"
>;

def FeatureSDWA : SubtargetFeature<"sdwa",
  "HasSDWA",
  "true",
  "Support SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWAOmod : SubtargetFeature<"sdwa-omod",
  "HasSDWAOmod",
  "true",
  "Support OMod with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWAScalar : SubtargetFeature<"sdwa-scalar",
  "HasSDWAScalar",
  "true",
  "Support scalar register with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWASdst : SubtargetFeature<"sdwa-sdst",
  "HasSDWASdst",
  "true",
  "Support scalar dst for VOPC with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWAMac : SubtargetFeature<"sdwa-mav",
  "HasSDWAMac",
  "true",
  "Support v_mac_f32/f16 with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureSDWAOutModsVOPC : SubtargetFeature<"sdwa-out-mods-vopc",
  "HasSDWAOutModsVOPC",
  "true",
  "Support clamp for VOPC with SDWA (Sub-DWORD Addressing) extension"
>;

def FeatureDPP : SubtargetFeature<"dpp",
  "HasDPP",
  "true",
  "Support DPP (Data Parallel Primitives) extension"
>;

def FeatureR128A16 : SubtargetFeature<"r128-a16",
  "HasR128A16",
  "true",
  "Support 16 bit coordindates/gradients/lod/clamp/mip types on gfx9"
>;

def FeatureIntClamp : SubtargetFeature<"int-clamp-insts",
  "HasIntClamp",
  "true",
  "Support clamp for integer destination"
>;

def FeatureUnpackedD16VMem : SubtargetFeature<"unpacked-d16-vmem",
  "HasUnpackedD16VMem",
  "true",
  "Has unpacked d16 vmem instructions"
>;

def FeatureDLInsts : SubtargetFeature<"dl-insts",
  "HasDLInsts",
  "true",
  "Has v_fmac_f32 and v_xnor_b32 instructions"
>;

def FeatureDotInsts : SubtargetFeature<"dot-insts",
  "HasDotInsts",
  "true",
  "Has v_dot* instructions"
>;

def FeatureSRAMECC : SubtargetFeature<"sram-ecc",
  "EnableSRAMECC",
  "true",
  "Enable SRAM ECC"
>;

//===------------------------------------------------------------===//
// Subtarget Features (options and debugging)
//===------------------------------------------------------------===//

// Denormal handling for fp64 and fp16 is controlled by the same
// config register when fp16 supported.
// TODO: Do we need a separate f16 setting when not legal?
def FeatureFP64FP16Denormals : SubtargetFeature<"fp64-fp16-denormals",
  "FP64FP16Denormals",
  "true",
  "Enable double and half precision denormal handling",
  [FeatureFP64]
>;

def FeatureFP64Denormals : SubtargetFeature<"fp64-denormals",
  "FP64FP16Denormals",
  "true",
  "Enable double and half precision denormal handling",
  [FeatureFP64, FeatureFP64FP16Denormals]
>;

def FeatureFP16Denormals : SubtargetFeature<"fp16-denormals",
  "FP64FP16Denormals",
  "true",
  "Enable half precision denormal handling",
  [FeatureFP64FP16Denormals]
>;

def FeatureFPExceptions : SubtargetFeature<"fp-exceptions",
  "FPExceptions",
  "true",
  "Enable floating point exceptions"
>;

class FeatureMaxPrivateElementSize<int size> : SubtargetFeature<
  "max-private-element-size-"#size,
  "MaxPrivateElementSize",
  !cast<string>(size),
  "Maximum private access size may be "#size
>;

def FeatureMaxPrivateElementSize4 : FeatureMaxPrivateElementSize<4>;
def FeatureMaxPrivateElementSize8 : FeatureMaxPrivateElementSize<8>;
def FeatureMaxPrivateElementSize16 : FeatureMaxPrivateElementSize<16>;

def FeatureEnableHugePrivateBuffer : SubtargetFeature<
  "huge-private-buffer",
  "EnableHugePrivateBuffer",
  "true",
  "Enable private/scratch buffer sizes greater than 128 GB"
>;

def FeatureDumpCode : SubtargetFeature <"DumpCode",
  "DumpCode",
  "true",
  "Dump MachineInstrs in the CodeEmitter"
>;

def FeatureDumpCodeLower : SubtargetFeature <"dumpcode",
  "DumpCode",
  "true",
  "Dump MachineInstrs in the CodeEmitter"
>;

// XXX - This should probably be removed once enabled by default
def FeatureEnableLoadStoreOpt : SubtargetFeature <"load-store-opt",
  "EnableLoadStoreOpt",
  "true",
  "Enable SI load/store optimizer pass"
>;

// Performance debugging feature. Allow using DS instruction immediate
// offsets even if the base pointer can't be proven to be base. On SI,
// base pointer values that won't give the same result as a 16-bit add
// are not safe to fold, but this will override the conservative test
// for the base pointer.
def FeatureEnableUnsafeDSOffsetFolding : SubtargetFeature <
  "unsafe-ds-offset-folding",
  "EnableUnsafeDSOffsetFolding",
  "true",
  "Force using DS instruction immediate offsets on SI"
>;

def FeatureEnableSIScheduler : SubtargetFeature<"si-scheduler",
  "EnableSIScheduler",
  "true",
  "Enable SI Machine Scheduler"
>;

def FeatureEnableDS128 : SubtargetFeature<"enable-ds128",
  "EnableDS128",
  "true",
  "Use ds_{read|write}_b128"
>;

// Sparse texture support requires that all result registers are zeroed when
// PRTStrictNull is set to true. This feature is turned on for all architectures
// but is enabled as a feature in case there are situations where PRTStrictNull
// is disabled by the driver.
def FeatureEnablePRTStrictNull : SubtargetFeature<"enable-prt-strict-null",
  "EnablePRTStrictNull",
  "true",
  "Enable zeroing of result registers for sparse texture fetches"
>;

// Unless +-flat-for-global is specified, turn on FlatForGlobal for
// all OS-es on VI and newer hardware to avoid assertion failures due
// to missing ADDR64 variants of MUBUF instructions.
// FIXME: moveToVALU should be able to handle converting addr64 MUBUF
// instructions.

def FeatureFlatForGlobal : SubtargetFeature<"flat-for-global",
  "FlatForGlobal",
  "true",
  "Force to generate flat instruction for global"
>;

def FeatureAutoWaitcntBeforeBarrier : SubtargetFeature <
  "auto-waitcnt-before-barrier",
  "AutoWaitcntBeforeBarrier",
  "true",
  "Hardware automatically inserts waitcnt before barrier"
>;

def FeatureCodeObjectV3 : SubtargetFeature <
  "code-object-v3",
  "CodeObjectV3",
  "true",
  "Generate code object version 3"
>;

def FeatureTrigReducedRange : SubtargetFeature<"trig-reduced-range",
  "HasTrigReducedRange",
  "true",
  "Requires use of fract on arguments to trig instructions"
>;

// Dummy feature used to disable assembler instructions.
def FeatureDisable : SubtargetFeature<"",
  "FeatureDisable","true",
  "Dummy feature to disable assembler instructions"
>;

def FeatureGCN : SubtargetFeature<"gcn",
  "IsGCN",
  "true",
  "GCN or newer GPU"
>;

class GCNSubtargetFeatureGeneration <string Value,
                                  list<SubtargetFeature> Implies> :
        SubtargetFeatureGeneration <Value, "GCNSubtarget", Implies>;

def FeatureSouthernIslands : GCNSubtargetFeatureGeneration<"SOUTHERN_ISLANDS",
  [FeatureFP64, FeatureLocalMemorySize32768, FeatureMIMG_R128,
  FeatureWavefrontSize64, FeatureGCN,
  FeatureLDSBankCount32, FeatureMovrel, FeatureTrigReducedRange]
>;

def FeatureSeaIslands : GCNSubtargetFeatureGeneration<"SEA_ISLANDS",
  [FeatureFP64, FeatureLocalMemorySize65536, FeatureMIMG_R128,
  FeatureWavefrontSize64, FeatureGCN, FeatureFlatAddressSpace,
  FeatureCIInsts, FeatureMovrel, FeatureTrigReducedRange]
>;

def FeatureVolcanicIslands : GCNSubtargetFeatureGeneration<"VOLCANIC_ISLANDS",
  [FeatureFP64, FeatureLocalMemorySize65536, FeatureMIMG_R128,
   FeatureWavefrontSize64, FeatureFlatAddressSpace, FeatureGCN,
   FeatureGCN3Encoding, FeatureCIInsts, FeatureVIInsts, Feature16BitInsts,
   FeatureSMemRealTime, FeatureVGPRIndexMode, FeatureMovrel,
   FeatureScalarStores, FeatureInv2PiInlineImm,
   FeatureSDWA, FeatureSDWAOutModsVOPC, FeatureSDWAMac, FeatureDPP,
   FeatureIntClamp, FeatureTrigReducedRange
  ]
>;

def FeatureGFX9 : GCNSubtargetFeatureGeneration<"GFX9",
  [FeatureFP64, FeatureLocalMemorySize65536,
   FeatureWavefrontSize64, FeatureFlatAddressSpace, FeatureGCN,
   FeatureGCN3Encoding, FeatureCIInsts, FeatureVIInsts, Feature16BitInsts,
   FeatureSMemRealTime, FeatureScalarStores, FeatureInv2PiInlineImm,
   FeatureApertureRegs, FeatureGFX9Insts, FeatureVOP3P, FeatureVGPRIndexMode,
   FeatureFastFMAF32, FeatureDPP, FeatureIntClamp,
   FeatureSDWA, FeatureSDWAOmod, FeatureSDWAScalar, FeatureSDWASdst,
   FeatureFlatInstOffsets, FeatureFlatGlobalInsts, FeatureFlatScratchInsts,
   FeatureAddNoCarryInsts, FeatureScalarAtomics, FeatureR128A16
  ]
>;

class SubtargetFeatureISAVersion <int Major, int Minor, int Stepping,
                                  list<SubtargetFeature> Implies>
                                 : SubtargetFeature <
  "isaver"#Major#"."#Minor#"."#Stepping,
  "IsaVersion",
  "ISAVersion"#Major#"_"#Minor#"_"#Stepping,
  "Instruction set version number",
  Implies
>;

def FeatureISAVersion6_0_0 : SubtargetFeatureISAVersion <6,0,0,
  [FeatureSouthernIslands,
   FeatureFastFMAF32,
   HalfRate64Ops,
   FeatureLDSBankCount32,
   FeatureCodeObjectV3]>;

def FeatureISAVersion6_0_1 : SubtargetFeatureISAVersion <6,0,1,
  [FeatureSouthernIslands,
   FeatureLDSBankCount32,
   FeatureCodeObjectV3]>;

def FeatureISAVersion7_0_0 : SubtargetFeatureISAVersion <7,0,0,
  [FeatureSeaIslands,
   FeatureLDSBankCount32,
   FeatureCodeObjectV3]>;

def FeatureISAVersion7_0_1 : SubtargetFeatureISAVersion <7,0,1,
  [FeatureSeaIslands,
   HalfRate64Ops,
   FeatureLDSBankCount32,
   FeatureFastFMAF32,
   FeatureCodeObjectV3]>;

def FeatureISAVersion7_0_2 : SubtargetFeatureISAVersion <7,0,2,
  [FeatureSeaIslands,
   FeatureLDSBankCount16,
   FeatureFastFMAF32,
   FeatureCodeObjectV3]>;

def FeatureISAVersion7_0_3 : SubtargetFeatureISAVersion <7,0,3,
  [FeatureSeaIslands,
   FeatureLDSBankCount16,
   FeatureCodeObjectV3]>;

def FeatureISAVersion7_0_4 : SubtargetFeatureISAVersion <7,0,4,
  [FeatureSeaIslands,
   FeatureLDSBankCount32,
   FeatureCodeObjectV3]>;

def FeatureISAVersion8_0_1 : SubtargetFeatureISAVersion <8,0,1,
  [FeatureVolcanicIslands,
   FeatureFastFMAF32,
   HalfRate64Ops,
   FeatureLDSBankCount32,
   FeatureXNACK,
   FeatureUnpackedD16VMem,
   FeatureCodeObjectV3]>;

def FeatureISAVersion8_0_2 : SubtargetFeatureISAVersion <8,0,2,
  [FeatureVolcanicIslands,
   FeatureLDSBankCount32,
   FeatureSGPRInitBug,
   FeatureUnpackedD16VMem,
   FeatureCodeObjectV3]>;

def FeatureISAVersion8_0_3 : SubtargetFeatureISAVersion <8,0,3,
  [FeatureVolcanicIslands,
   FeatureLDSBankCount32,
   FeatureUnpackedD16VMem,
   FeatureCodeObjectV3]>;

def FeatureISAVersion8_1_0 : SubtargetFeatureISAVersion <8,1,0,
  [FeatureVolcanicIslands,
   FeatureLDSBankCount16,
   FeatureXNACK,
   FeatureCodeObjectV3]>;

def FeatureISAVersion9_0_0 : SubtargetFeatureISAVersion <9,0,0,
  [FeatureGFX9,
   FeatureMadMixInsts,
   FeatureLDSBankCount32,
   FeatureCodeObjectV3]>;

def FeatureISAVersion9_0_2 : SubtargetFeatureISAVersion <9,0,2,
  [FeatureGFX9,
   FeatureMadMixInsts,
   FeatureLDSBankCount32,
   FeatureXNACK,
   FeatureCodeObjectV3]>;

def FeatureISAVersion9_0_4 : SubtargetFeatureISAVersion <9,0,4,
  [FeatureGFX9,
   FeatureLDSBankCount32,
   FeatureFmaMixInsts,
   FeatureCodeObjectV3]>;

def FeatureISAVersion9_0_6 : SubtargetFeatureISAVersion <9,0,6,
  [FeatureGFX9,
   HalfRate64Ops,
   FeatureFmaMixInsts,
   FeatureLDSBankCount32,
   FeatureDLInsts,
   FeatureDotInsts,
   FeatureSRAMECC,
   FeatureCodeObjectV3]>;

def FeatureISAVersion9_0_9 : SubtargetFeatureISAVersion <9,0,9,
  [FeatureGFX9,
   FeatureMadMixInsts,
   FeatureLDSBankCount32,
   FeatureXNACK,
   FeatureCodeObjectV3]>;

//===----------------------------------------------------------------------===//
// Debugger related subtarget features.
//===----------------------------------------------------------------------===//

def FeatureDebuggerInsertNops : SubtargetFeature<
  "amdgpu-debugger-insert-nops",
  "DebuggerInsertNops",
  "true",
  "Insert one nop instruction for each high level source statement"
>;

def FeatureDebuggerEmitPrologue : SubtargetFeature<
  "amdgpu-debugger-emit-prologue",
  "DebuggerEmitPrologue",
  "true",
  "Emit debugger prologue"
>;

//===----------------------------------------------------------------------===//

def AMDGPUInstrInfo : InstrInfo {
  let guessInstructionProperties = 1;
  let noNamedPositionallyEncodedOperands = 1;
}

def AMDGPUAsmParser : AsmParser {
  // Some of the R600 registers have the same name, so this crashes.
  // For example T0_XYZW and T0_XY both have the asm name T0.
  let ShouldEmitMatchRegisterName = 0;
}

def AMDGPUAsmWriter : AsmWriter {
  int PassSubtarget = 1;
}

def AMDGPUAsmVariants {
  string Default = "Default";
  int Default_ID = 0;
  string VOP3 = "VOP3";
  int VOP3_ID = 1;
  string SDWA = "SDWA";
  int SDWA_ID = 2;
  string SDWA9 = "SDWA9";
  int SDWA9_ID = 3;
  string DPP = "DPP";
  int DPP_ID = 4;
  string Disable = "Disable";
  int Disable_ID = 5;
}

def DefaultAMDGPUAsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.Default_ID;
  let Name = AMDGPUAsmVariants.Default;
}

def VOP3AsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.VOP3_ID;
  let Name = AMDGPUAsmVariants.VOP3;
}

def SDWAAsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.SDWA_ID;
  let Name = AMDGPUAsmVariants.SDWA;
}

def SDWA9AsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.SDWA9_ID;
  let Name = AMDGPUAsmVariants.SDWA9;
}


def DPPAsmParserVariant : AsmParserVariant {
  let Variant = AMDGPUAsmVariants.DPP_ID;
  let Name = AMDGPUAsmVariants.DPP;
}

def AMDGPU : Target {
  // Pull in Instruction Info:
  let InstructionSet = AMDGPUInstrInfo;
  let AssemblyParsers = [AMDGPUAsmParser];
  let AssemblyParserVariants = [DefaultAMDGPUAsmParserVariant,
                                VOP3AsmParserVariant,
                                SDWAAsmParserVariant,
                                SDWA9AsmParserVariant,
                                DPPAsmParserVariant];
  let AssemblyWriters = [AMDGPUAsmWriter];
  let AllowRegisterRenaming = 1;
}

// Dummy Instruction itineraries for pseudo instructions
def ALU_NULL : FuncUnit;
def NullALU : InstrItinClass;

//===----------------------------------------------------------------------===//
// Predicate helper class
//===----------------------------------------------------------------------===//

def isSICI : Predicate<
  "Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS ||"
  "Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS"
>, AssemblerPredicate<"!FeatureGCN3Encoding">;

def isVI : Predicate <
  "Subtarget->getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS">,
  AssemblerPredicate<"FeatureGCN3Encoding">;

def isGFX9 : Predicate <
  "Subtarget->getGeneration() >= AMDGPUSubtarget::GFX9">,
  AssemblerPredicate<"FeatureGFX9Insts">;

// TODO: Either the name to be changed or we simply use IsCI!
def isCIVI : Predicate <
  "Subtarget->getGeneration() >= AMDGPUSubtarget::SEA_ISLANDS">,
  AssemblerPredicate<"FeatureCIInsts">;

def HasFlatAddressSpace : Predicate<"Subtarget->hasFlatAddressSpace()">,
  AssemblerPredicate<"FeatureFlatAddressSpace">;

def HasFlatGlobalInsts : Predicate<"Subtarget->hasFlatGlobalInsts()">,
  AssemblerPredicate<"FeatureFlatGlobalInsts">;
def HasFlatScratchInsts : Predicate<"Subtarget->hasFlatScratchInsts()">,
  AssemblerPredicate<"FeatureFlatScratchInsts">;
def HasD16LoadStore : Predicate<"Subtarget->hasD16LoadStore()">,
  AssemblerPredicate<"FeatureGFX9Insts">;

def HasUnpackedD16VMem : Predicate<"Subtarget->hasUnpackedD16VMem()">,
  AssemblerPredicate<"FeatureUnpackedD16VMem">;
def HasPackedD16VMem : Predicate<"!Subtarget->hasUnpackedD16VMem()">,
  AssemblerPredicate<"!FeatureUnpackedD16VMem">;

def D16PreservesUnusedBits :
  Predicate<"Subtarget->hasD16LoadStore() && !Subtarget->isSRAMECCEnabled()">,
  AssemblerPredicate<"FeatureGFX9Insts,!FeatureSRAMECC">;

def LDSRequiresM0Init : Predicate<"Subtarget->ldsRequiresM0Init()">;
def NotLDSRequiresM0Init : Predicate<"!Subtarget->ldsRequiresM0Init()">;

def HasDSAddTid : Predicate<"Subtarget->getGeneration() >= AMDGPUSubtarget::GFX9">,
  AssemblerPredicate<"FeatureGFX9Insts">;

def HasAddNoCarryInsts : Predicate<"Subtarget->hasAddNoCarry()">,
  AssemblerPredicate<"FeatureAddNoCarryInsts">;

def NotHasAddNoCarryInsts : Predicate<"!Subtarget->hasAddNoCarry()">,
  AssemblerPredicate<"!FeatureAddNoCarryInsts">;

def Has16BitInsts : Predicate<"Subtarget->has16BitInsts()">,
  AssemblerPredicate<"Feature16BitInsts">;
def HasVOP3PInsts : Predicate<"Subtarget->hasVOP3PInsts()">,
  AssemblerPredicate<"FeatureVOP3P">;

def NotHasVOP3PInsts : Predicate<"!Subtarget->hasVOP3PInsts()">,
  AssemblerPredicate<"!FeatureVOP3P">;

def HasSDWA : Predicate<"Subtarget->hasSDWA()">,
  AssemblerPredicate<"FeatureSDWA,FeatureVolcanicIslands">;

def HasSDWA9 : Predicate<"Subtarget->hasSDWA()">,
  AssemblerPredicate<"FeatureSDWA,FeatureGFX9">;

def HasDPP : Predicate<"Subtarget->hasDPP()">,
  AssemblerPredicate<"FeatureDPP">;

def HasR128A16 : Predicate<"Subtarget->hasR128A16()">,
  AssemblerPredicate<"FeatureR128A16">;

def HasIntClamp : Predicate<"Subtarget->hasIntClamp()">,
  AssemblerPredicate<"FeatureIntClamp">;

def HasMadMixInsts : Predicate<"Subtarget->hasMadMixInsts()">,
  AssemblerPredicate<"FeatureMadMixInsts">;

def HasScalarAtomics : Predicate<"Subtarget->hasScalarAtomics()">,
  AssemblerPredicate<"FeatureScalarAtomics">;

def has16BankLDS : Predicate<"Subtarget->getLDSBankCount() == 16">;
def has32BankLDS : Predicate<"Subtarget->getLDSBankCount() == 32">;
def HasVGPRIndexMode : Predicate<"Subtarget->hasVGPRIndexMode()">,
                      AssemblerPredicate<"FeatureVGPRIndexMode">;
def HasMovrel : Predicate<"Subtarget->hasMovrel()">,
                AssemblerPredicate<"FeatureMovrel">;

def HasFmaMixInsts : Predicate<"Subtarget->hasFmaMixInsts()">,
  AssemblerPredicate<"FeatureFmaMixInsts">;

def HasDLInsts : Predicate<"Subtarget->hasDLInsts()">,
  AssemblerPredicate<"FeatureDLInsts">;

def HasDotInsts : Predicate<"Subtarget->hasDotInsts()">,
  AssemblerPredicate<"FeatureDotInsts">;


def EnableLateCFGStructurize : Predicate<
  "EnableLateStructurizeCFG">;

// Include AMDGPU TD files
include "SISchedule.td"
include "GCNProcessors.td"
include "AMDGPUInstrInfo.td"
include "SIIntrinsics.td"
include "AMDGPURegisterInfo.td"
include "AMDGPURegisterBanks.td"
include "AMDGPUInstructions.td"
include "SIInstrInfo.td"
include "AMDGPUCallingConv.td"
include "AMDGPUSearchableTables.td"
