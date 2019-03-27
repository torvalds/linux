//===-- ARMBuildAttributes.h - ARM Build Attributes -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains enumerations and support routines for ARM build attributes
// as defined in ARM ABI addenda document (ABI release 2.08).
//
// ELF for the ARM Architecture r2.09 - November 30, 2012
//
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0044e/IHI0044E_aaelf.pdf
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ARMBUILDATTRIBUTES_H
#define LLVM_SUPPORT_ARMBUILDATTRIBUTES_H

namespace llvm {
class StringRef;

namespace ARMBuildAttrs {

enum SpecialAttr {
  // This is for the .cpu asm attr. It translates into one or more
  // AttrType (below) entries in the .ARM.attributes section in the ELF.
  SEL_CPU
};

enum AttrType {
  // Rest correspond to ELF/.ARM.attributes
  File                      = 1,
  CPU_raw_name              = 4,
  CPU_name                  = 5,
  CPU_arch                  = 6,
  CPU_arch_profile          = 7,
  ARM_ISA_use               = 8,
  THUMB_ISA_use             = 9,
  FP_arch                   = 10,
  WMMX_arch                 = 11,
  Advanced_SIMD_arch        = 12,
  PCS_config                = 13,
  ABI_PCS_R9_use            = 14,
  ABI_PCS_RW_data           = 15,
  ABI_PCS_RO_data           = 16,
  ABI_PCS_GOT_use           = 17,
  ABI_PCS_wchar_t           = 18,
  ABI_FP_rounding           = 19,
  ABI_FP_denormal           = 20,
  ABI_FP_exceptions         = 21,
  ABI_FP_user_exceptions    = 22,
  ABI_FP_number_model       = 23,
  ABI_align_needed          = 24,
  ABI_align_preserved       = 25,
  ABI_enum_size             = 26,
  ABI_HardFP_use            = 27,
  ABI_VFP_args              = 28,
  ABI_WMMX_args             = 29,
  ABI_optimization_goals    = 30,
  ABI_FP_optimization_goals = 31,
  compatibility             = 32,
  CPU_unaligned_access      = 34,
  FP_HP_extension           = 36,
  ABI_FP_16bit_format       = 38,
  MPextension_use           = 42, // recoded from 70 (ABI r2.08)
  DIV_use                   = 44,
  DSP_extension             = 46,
  also_compatible_with      = 65,
  conformance               = 67,
  Virtualization_use        = 68,

  /// Legacy Tags
  Section                   = 2,  // deprecated (ABI r2.09)
  Symbol                    = 3,  // deprecated (ABI r2.09)
  ABI_align8_needed         = 24, // renamed to ABI_align_needed (ABI r2.09)
  ABI_align8_preserved      = 25, // renamed to ABI_align_preserved (ABI r2.09)
  nodefaults                = 64, // deprecated (ABI r2.09)
  T2EE_use                  = 66, // deprecated (ABI r2.09)
  MPextension_use_old       = 70  // recoded to MPextension_use (ABI r2.08)
};

StringRef AttrTypeAsString(unsigned Attr, bool HasTagPrefix = true);
StringRef AttrTypeAsString(AttrType Attr, bool HasTagPrefix = true);
int AttrTypeFromString(StringRef Tag);

// Magic numbers for .ARM.attributes
enum AttrMagic {
  Format_Version  = 0x41
};

// Legal Values for CPU_arch, (=6), uleb128
enum CPUArch {
  Pre_v4   = 0,
  v4       = 1,   // e.g. SA110
  v4T      = 2,   // e.g. ARM7TDMI
  v5T      = 3,   // e.g. ARM9TDMI
  v5TE     = 4,   // e.g. ARM946E_S
  v5TEJ    = 5,   // e.g. ARM926EJ_S
  v6       = 6,   // e.g. ARM1136J_S
  v6KZ     = 7,   // e.g. ARM1176JZ_S
  v6T2     = 8,   // e.g. ARM1156T2_S
  v6K      = 9,   // e.g. ARM1176JZ_S
  v7       = 10,  // e.g. Cortex A8, Cortex M3
  v6_M     = 11,  // e.g. Cortex M1
  v6S_M    = 12,  // v6_M with the System extensions
  v7E_M    = 13,  // v7_M with DSP extensions
  v8_A     = 14,  // v8_A AArch32
  v8_R     = 15,  // e.g. Cortex R52
  v8_M_Base= 16,  // v8_M_Base AArch32
  v8_M_Main= 17,  // v8_M_Main AArch32
};

enum CPUArchProfile {               // (=7), uleb128
  Not_Applicable          = 0,      // pre v7, or cross-profile code
  ApplicationProfile      = (0x41), // 'A' (e.g. for Cortex A8)
  RealTimeProfile         = (0x52), // 'R' (e.g. for Cortex R4)
  MicroControllerProfile  = (0x4D), // 'M' (e.g. for Cortex M3)
  SystemProfile           = (0x53)  // 'S' Application or real-time profile
};

// The following have a lot of common use cases
enum {
  Not_Allowed = 0,
  Allowed = 1,

  // Tag_ARM_ISA_use (=8), uleb128

  // Tag_THUMB_ISA_use, (=9), uleb128
  AllowThumb32 = 2, // 32-bit Thumb (implies 16-bit instructions)
  AllowThumbDerived = 3, // Thumb allowed, derived from arch/profile

  // Tag_FP_arch (=10), uleb128 (formerly Tag_VFP_arch = 10)
  AllowFPv2  = 2,     // v2 FP ISA permitted (implies use of the v1 FP ISA)
  AllowFPv3A = 3,     // v3 FP ISA permitted (implies use of the v2 FP ISA)
  AllowFPv3B = 4,     // v3 FP ISA permitted, but only D0-D15, S0-S31
  AllowFPv4A = 5,     // v4 FP ISA permitted (implies use of v3 FP ISA)
  AllowFPv4B = 6,     // v4 FP ISA was permitted, but only D0-D15, S0-S31
  AllowFPARMv8A = 7,  // Use of the ARM v8-A FP ISA was permitted
  AllowFPARMv8B = 8,  // Use of the ARM v8-A FP ISA was permitted, but only
                      // D0-D15, S0-S31

  // Tag_WMMX_arch, (=11), uleb128
  AllowWMMXv1 = 1,  // The user permitted this entity to use WMMX v1
  AllowWMMXv2 = 2,  // The user permitted this entity to use WMMX v2

  // Tag_Advanced_SIMD_arch, (=12), uleb128
  AllowNeon = 1,      // SIMDv1 was permitted
  AllowNeon2 = 2,     // SIMDv2 was permitted (Half-precision FP, MAC operations)
  AllowNeonARMv8 = 3, // ARM v8-A SIMD was permitted
  AllowNeonARMv8_1a = 4,// ARM v8.1-A SIMD was permitted (RDMA)

  // Tag_ABI_PCS_R9_use, (=14), uleb128
  R9IsGPR = 0,        // R9 used as v6 (just another callee-saved register)
  R9IsSB = 1,         // R9 used as a global static base rgister
  R9IsTLSPointer = 2, // R9 used as a thread local storage pointer
  R9Reserved = 3,     // R9 not used by code associated with attributed entity

  // Tag_ABI_PCS_RW_data, (=15), uleb128
  AddressRWPCRel = 1, // Address RW static data PC-relative
  AddressRWSBRel = 2, // Address RW static data SB-relative
  AddressRWNone = 3, // No RW static data permitted

  // Tag_ABI_PCS_RO_data, (=14), uleb128
  AddressROPCRel = 1, // Address RO static data PC-relative
  AddressRONone = 2, // No RO static data permitted

  // Tag_ABI_PCS_GOT_use, (=17), uleb128
  AddressDirect = 1, // Address imported data directly
  AddressGOT = 2, // Address imported data indirectly (via GOT)

  // Tag_ABI_PCS_wchar_t, (=18), uleb128
  WCharProhibited = 0,  // wchar_t is not used
  WCharWidth2Bytes = 2, // sizeof(wchar_t) == 2
  WCharWidth4Bytes = 4, // sizeof(wchar_t) == 4

  // Tag_ABI_align_needed, (=24), uleb128
  Align8Byte = 1,
  Align4Byte = 2,
  AlignReserved = 3,

  // Tag_ABI_align_needed, (=25), uleb128
  AlignNotPreserved = 0,
  AlignPreserve8Byte = 1,
  AlignPreserveAll = 2,

  // Tag_ABI_FP_denormal, (=20), uleb128
  PositiveZero = 0,
  IEEEDenormals = 1,
  PreserveFPSign = 2, // sign when flushed-to-zero is preserved

  // Tag_ABI_FP_number_model, (=23), uleb128
  AllowIEEENormal = 1,
  AllowRTABI = 2,  // numbers, infinities, and one quiet NaN (see [RTABI])
  AllowIEEE754 = 3, // this code to use all the IEEE 754-defined FP encodings

  // Tag_ABI_enum_size, (=26), uleb128
  EnumProhibited = 0, // The user prohibited the use of enums when building
                      // this entity.
  EnumSmallest = 1,   // Enum is smallest container big enough to hold all
                      // values.
  Enum32Bit = 2,      // Enum is at least 32 bits.
  Enum32BitABI = 3,   // Every enumeration visible across an ABI-complying
                      // interface contains a value needing 32 bits to encode
                      // it; other enums can be containerized.

  // Tag_ABI_HardFP_use, (=27), uleb128
  HardFPImplied = 0,          // FP use should be implied by Tag_FP_arch
  HardFPSinglePrecision = 1,  // Single-precision only

  // Tag_ABI_VFP_args, (=28), uleb128
  BaseAAPCS = 0,
  HardFPAAPCS = 1,
  ToolChainFPPCS = 2,
  CompatibleFPAAPCS = 3,

  // Tag_FP_HP_extension, (=36), uleb128
  AllowHPFP = 1, // Allow use of Half Precision FP

  // Tag_FP_16bit_format, (=38), uleb128
  FP16FormatIEEE = 1,
  FP16VFP3 = 2,

  // Tag_MPextension_use, (=42), uleb128
  AllowMP = 1, // Allow use of MP extensions

  // Tag_DIV_use, (=44), uleb128
  // Note: AllowDIVExt must be emitted if and only if the permission to use
  // hardware divide cannot be conveyed using AllowDIVIfExists or DisallowDIV
  AllowDIVIfExists = 0, // Allow hardware divide if available in arch, or no
                        // info exists.
  DisallowDIV = 1,      // Hardware divide explicitly disallowed.
  AllowDIVExt = 2,      // Allow hardware divide as optional architecture
                        // extension above the base arch specified by
                        // Tag_CPU_arch and Tag_CPU_arch_profile.

  // Tag_Virtualization_use, (=68), uleb128
  AllowTZ = 1,
  AllowVirtualization = 2,
  AllowTZVirtualization = 3
};

} // namespace ARMBuildAttrs
} // namespace llvm

#endif
