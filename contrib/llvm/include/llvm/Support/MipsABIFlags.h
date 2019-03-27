//===--- MipsABIFlags.h - MIPS ABI flags ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the constants for the ABI flags structure contained
// in the .MIPS.abiflags section.
//
// https://dmz-portal.mips.com/wiki/MIPS_O32_ABI_-_FR0_and_FR1_Interlinking
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_MIPSABIFLAGS_H
#define LLVM_SUPPORT_MIPSABIFLAGS_H

namespace llvm {
namespace Mips {

// Values for the xxx_size bytes of an ABI flags structure.
enum AFL_REG {
  AFL_REG_NONE = 0x00, // No registers
  AFL_REG_32 = 0x01,   // 32-bit registers
  AFL_REG_64 = 0x02,   // 64-bit registers
  AFL_REG_128 = 0x03   // 128-bit registers
};

// Masks for the ases word of an ABI flags structure.
enum AFL_ASE {
  AFL_ASE_DSP = 0x00000001,       // DSP ASE
  AFL_ASE_DSPR2 = 0x00000002,     // DSP R2 ASE
  AFL_ASE_EVA = 0x00000004,       // Enhanced VA Scheme
  AFL_ASE_MCU = 0x00000008,       // MCU (MicroController) ASE
  AFL_ASE_MDMX = 0x00000010,      // MDMX ASE
  AFL_ASE_MIPS3D = 0x00000020,    // MIPS-3D ASE
  AFL_ASE_MT = 0x00000040,        // MT ASE
  AFL_ASE_SMARTMIPS = 0x00000080, // SmartMIPS ASE
  AFL_ASE_VIRT = 0x00000100,      // VZ ASE
  AFL_ASE_MSA = 0x00000200,       // MSA ASE
  AFL_ASE_MIPS16 = 0x00000400,    // MIPS16 ASE
  AFL_ASE_MICROMIPS = 0x00000800, // MICROMIPS ASE
  AFL_ASE_XPA = 0x00001000,       // XPA ASE
  AFL_ASE_CRC = 0x00008000,       // CRC ASE
  AFL_ASE_GINV = 0x00020000       // GINV ASE
};

// Values for the isa_ext word of an ABI flags structure.
enum AFL_EXT {
  AFL_EXT_NONE = 0,         // None
  AFL_EXT_XLR = 1,          // RMI Xlr instruction
  AFL_EXT_OCTEON2 = 2,      // Cavium Networks Octeon2
  AFL_EXT_OCTEONP = 3,      // Cavium Networks OcteonP
  AFL_EXT_LOONGSON_3A = 4,  // Loongson 3A
  AFL_EXT_OCTEON = 5,       // Cavium Networks Octeon
  AFL_EXT_5900 = 6,         // MIPS R5900 instruction
  AFL_EXT_4650 = 7,         // MIPS R4650 instruction
  AFL_EXT_4010 = 8,         // LSI R4010 instruction
  AFL_EXT_4100 = 9,         // NEC VR4100 instruction
  AFL_EXT_3900 = 10,        // Toshiba R3900 instruction
  AFL_EXT_10000 = 11,       // MIPS R10000 instruction
  AFL_EXT_SB1 = 12,         // Broadcom SB-1 instruction
  AFL_EXT_4111 = 13,        // NEC VR4111/VR4181 instruction
  AFL_EXT_4120 = 14,        // NEC VR4120 instruction
  AFL_EXT_5400 = 15,        // NEC VR5400 instruction
  AFL_EXT_5500 = 16,        // NEC VR5500 instruction
  AFL_EXT_LOONGSON_2E = 17, // ST Microelectronics Loongson 2E
  AFL_EXT_LOONGSON_2F = 18, // ST Microelectronics Loongson 2F
  AFL_EXT_OCTEON3 = 19      // Cavium Networks Octeon3
};

// Values for the flags1 word of an ABI flags structure.
enum AFL_FLAGS1 { AFL_FLAGS1_ODDSPREG = 1 };

// MIPS object attribute tags
enum {
  Tag_GNU_MIPS_ABI_FP = 4,  // Floating-point ABI used by this object file
  Tag_GNU_MIPS_ABI_MSA = 8, // MSA ABI used by this object file
};

// Values for the fp_abi word of an ABI flags structure
// and for the Tag_GNU_MIPS_ABI_FP attribute tag.
enum Val_GNU_MIPS_ABI_FP {
  Val_GNU_MIPS_ABI_FP_ANY = 0,    // not tagged
  Val_GNU_MIPS_ABI_FP_DOUBLE = 1, // hard float / -mdouble-float
  Val_GNU_MIPS_ABI_FP_SINGLE = 2, // hard float / -msingle-float
  Val_GNU_MIPS_ABI_FP_SOFT = 3,   // soft float
  Val_GNU_MIPS_ABI_FP_OLD_64 = 4, // -mips32r2 -mfp64
  Val_GNU_MIPS_ABI_FP_XX = 5,     // -mfpxx
  Val_GNU_MIPS_ABI_FP_64 = 6,     // -mips32r2 -mfp64
  Val_GNU_MIPS_ABI_FP_64A = 7     // -mips32r2 -mfp64 -mno-odd-spreg
};

// Values for the Tag_GNU_MIPS_ABI_MSA attribute tag.
enum Val_GNU_MIPS_ABI_MSA {
  Val_GNU_MIPS_ABI_MSA_ANY = 0, // not tagged
  Val_GNU_MIPS_ABI_MSA_128 = 1  // 128-bit MSA
};
}
}

#endif
