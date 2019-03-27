//===- ELFYAML.cpp - ELF YAMLIO implementation ----------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of ELF.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/ELFYAML.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MipsABIFlags.h"
#include "llvm/Support/YAMLTraits.h"
#include <cassert>
#include <cstdint>

namespace llvm {

ELFYAML::Section::~Section() = default;

namespace yaml {

void ScalarEnumerationTraits<ELFYAML::ELF_ET>::enumeration(
    IO &IO, ELFYAML::ELF_ET &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(ET_NONE);
  ECase(ET_REL);
  ECase(ET_EXEC);
  ECase(ET_DYN);
  ECase(ET_CORE);
#undef ECase
  IO.enumFallback<Hex16>(Value);
}

void ScalarEnumerationTraits<ELFYAML::ELF_PT>::enumeration(
    IO &IO, ELFYAML::ELF_PT &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(PT_NULL);
  ECase(PT_LOAD);
  ECase(PT_DYNAMIC);
  ECase(PT_INTERP);
  ECase(PT_NOTE);
  ECase(PT_SHLIB);
  ECase(PT_PHDR);
  ECase(PT_TLS);
  ECase(PT_GNU_EH_FRAME);
#undef ECase
  IO.enumFallback<Hex32>(Value);
}

void ScalarEnumerationTraits<ELFYAML::ELF_EM>::enumeration(
    IO &IO, ELFYAML::ELF_EM &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(EM_NONE);
  ECase(EM_M32);
  ECase(EM_SPARC);
  ECase(EM_386);
  ECase(EM_68K);
  ECase(EM_88K);
  ECase(EM_IAMCU);
  ECase(EM_860);
  ECase(EM_MIPS);
  ECase(EM_S370);
  ECase(EM_MIPS_RS3_LE);
  ECase(EM_PARISC);
  ECase(EM_VPP500);
  ECase(EM_SPARC32PLUS);
  ECase(EM_960);
  ECase(EM_PPC);
  ECase(EM_PPC64);
  ECase(EM_S390);
  ECase(EM_SPU);
  ECase(EM_V800);
  ECase(EM_FR20);
  ECase(EM_RH32);
  ECase(EM_RCE);
  ECase(EM_ARM);
  ECase(EM_ALPHA);
  ECase(EM_SH);
  ECase(EM_SPARCV9);
  ECase(EM_TRICORE);
  ECase(EM_ARC);
  ECase(EM_H8_300);
  ECase(EM_H8_300H);
  ECase(EM_H8S);
  ECase(EM_H8_500);
  ECase(EM_IA_64);
  ECase(EM_MIPS_X);
  ECase(EM_COLDFIRE);
  ECase(EM_68HC12);
  ECase(EM_MMA);
  ECase(EM_PCP);
  ECase(EM_NCPU);
  ECase(EM_NDR1);
  ECase(EM_STARCORE);
  ECase(EM_ME16);
  ECase(EM_ST100);
  ECase(EM_TINYJ);
  ECase(EM_X86_64);
  ECase(EM_PDSP);
  ECase(EM_PDP10);
  ECase(EM_PDP11);
  ECase(EM_FX66);
  ECase(EM_ST9PLUS);
  ECase(EM_ST7);
  ECase(EM_68HC16);
  ECase(EM_68HC11);
  ECase(EM_68HC08);
  ECase(EM_68HC05);
  ECase(EM_SVX);
  ECase(EM_ST19);
  ECase(EM_VAX);
  ECase(EM_CRIS);
  ECase(EM_JAVELIN);
  ECase(EM_FIREPATH);
  ECase(EM_ZSP);
  ECase(EM_MMIX);
  ECase(EM_HUANY);
  ECase(EM_PRISM);
  ECase(EM_AVR);
  ECase(EM_FR30);
  ECase(EM_D10V);
  ECase(EM_D30V);
  ECase(EM_V850);
  ECase(EM_M32R);
  ECase(EM_MN10300);
  ECase(EM_MN10200);
  ECase(EM_PJ);
  ECase(EM_OPENRISC);
  ECase(EM_ARC_COMPACT);
  ECase(EM_XTENSA);
  ECase(EM_VIDEOCORE);
  ECase(EM_TMM_GPP);
  ECase(EM_NS32K);
  ECase(EM_TPC);
  ECase(EM_SNP1K);
  ECase(EM_ST200);
  ECase(EM_IP2K);
  ECase(EM_MAX);
  ECase(EM_CR);
  ECase(EM_F2MC16);
  ECase(EM_MSP430);
  ECase(EM_BLACKFIN);
  ECase(EM_SE_C33);
  ECase(EM_SEP);
  ECase(EM_ARCA);
  ECase(EM_UNICORE);
  ECase(EM_EXCESS);
  ECase(EM_DXP);
  ECase(EM_ALTERA_NIOS2);
  ECase(EM_CRX);
  ECase(EM_XGATE);
  ECase(EM_C166);
  ECase(EM_M16C);
  ECase(EM_DSPIC30F);
  ECase(EM_CE);
  ECase(EM_M32C);
  ECase(EM_TSK3000);
  ECase(EM_RS08);
  ECase(EM_SHARC);
  ECase(EM_ECOG2);
  ECase(EM_SCORE7);
  ECase(EM_DSP24);
  ECase(EM_VIDEOCORE3);
  ECase(EM_LATTICEMICO32);
  ECase(EM_SE_C17);
  ECase(EM_TI_C6000);
  ECase(EM_TI_C2000);
  ECase(EM_TI_C5500);
  ECase(EM_MMDSP_PLUS);
  ECase(EM_CYPRESS_M8C);
  ECase(EM_R32C);
  ECase(EM_TRIMEDIA);
  ECase(EM_HEXAGON);
  ECase(EM_8051);
  ECase(EM_STXP7X);
  ECase(EM_NDS32);
  ECase(EM_ECOG1);
  ECase(EM_ECOG1X);
  ECase(EM_MAXQ30);
  ECase(EM_XIMO16);
  ECase(EM_MANIK);
  ECase(EM_CRAYNV2);
  ECase(EM_RX);
  ECase(EM_METAG);
  ECase(EM_MCST_ELBRUS);
  ECase(EM_ECOG16);
  ECase(EM_CR16);
  ECase(EM_ETPU);
  ECase(EM_SLE9X);
  ECase(EM_L10M);
  ECase(EM_K10M);
  ECase(EM_AARCH64);
  ECase(EM_AVR32);
  ECase(EM_STM8);
  ECase(EM_TILE64);
  ECase(EM_TILEPRO);
  ECase(EM_CUDA);
  ECase(EM_TILEGX);
  ECase(EM_CLOUDSHIELD);
  ECase(EM_COREA_1ST);
  ECase(EM_COREA_2ND);
  ECase(EM_ARC_COMPACT2);
  ECase(EM_OPEN8);
  ECase(EM_RL78);
  ECase(EM_VIDEOCORE5);
  ECase(EM_78KOR);
  ECase(EM_56800EX);
  ECase(EM_AMDGPU);
  ECase(EM_RISCV);
  ECase(EM_LANAI);
  ECase(EM_BPF);
#undef ECase
}

void ScalarEnumerationTraits<ELFYAML::ELF_ELFCLASS>::enumeration(
    IO &IO, ELFYAML::ELF_ELFCLASS &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  // Since the semantics of ELFCLASSNONE is "invalid", just don't accept it
  // here.
  ECase(ELFCLASS32);
  ECase(ELFCLASS64);
#undef ECase
}

void ScalarEnumerationTraits<ELFYAML::ELF_ELFDATA>::enumeration(
    IO &IO, ELFYAML::ELF_ELFDATA &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  // Since the semantics of ELFDATANONE is "invalid", just don't accept it
  // here.
  ECase(ELFDATA2LSB);
  ECase(ELFDATA2MSB);
#undef ECase
}

void ScalarEnumerationTraits<ELFYAML::ELF_ELFOSABI>::enumeration(
    IO &IO, ELFYAML::ELF_ELFOSABI &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(ELFOSABI_NONE);
  ECase(ELFOSABI_HPUX);
  ECase(ELFOSABI_NETBSD);
  ECase(ELFOSABI_GNU);
  ECase(ELFOSABI_HURD);
  ECase(ELFOSABI_SOLARIS);
  ECase(ELFOSABI_AIX);
  ECase(ELFOSABI_IRIX);
  ECase(ELFOSABI_FREEBSD);
  ECase(ELFOSABI_TRU64);
  ECase(ELFOSABI_MODESTO);
  ECase(ELFOSABI_OPENBSD);
  ECase(ELFOSABI_OPENVMS);
  ECase(ELFOSABI_NSK);
  ECase(ELFOSABI_AROS);
  ECase(ELFOSABI_FENIXOS);
  ECase(ELFOSABI_CLOUDABI);
  ECase(ELFOSABI_AMDGPU_HSA);
  ECase(ELFOSABI_AMDGPU_PAL);
  ECase(ELFOSABI_AMDGPU_MESA3D);
  ECase(ELFOSABI_ARM);
  ECase(ELFOSABI_C6000_ELFABI);
  ECase(ELFOSABI_C6000_LINUX);
  ECase(ELFOSABI_STANDALONE);
#undef ECase
}

void ScalarBitSetTraits<ELFYAML::ELF_EF>::bitset(IO &IO,
                                                 ELFYAML::ELF_EF &Value) {
  const auto *Object = static_cast<ELFYAML::Object *>(IO.getContext());
  assert(Object && "The IO context is not initialized");
#define BCase(X) IO.bitSetCase(Value, #X, ELF::X)
#define BCaseMask(X, M) IO.maskedBitSetCase(Value, #X, ELF::X, ELF::M)
  switch (Object->Header.Machine) {
  case ELF::EM_ARM:
    BCase(EF_ARM_SOFT_FLOAT);
    BCase(EF_ARM_VFP_FLOAT);
    BCaseMask(EF_ARM_EABI_UNKNOWN, EF_ARM_EABIMASK);
    BCaseMask(EF_ARM_EABI_VER1, EF_ARM_EABIMASK);
    BCaseMask(EF_ARM_EABI_VER2, EF_ARM_EABIMASK);
    BCaseMask(EF_ARM_EABI_VER3, EF_ARM_EABIMASK);
    BCaseMask(EF_ARM_EABI_VER4, EF_ARM_EABIMASK);
    BCaseMask(EF_ARM_EABI_VER5, EF_ARM_EABIMASK);
    break;
  case ELF::EM_MIPS:
    BCase(EF_MIPS_NOREORDER);
    BCase(EF_MIPS_PIC);
    BCase(EF_MIPS_CPIC);
    BCase(EF_MIPS_ABI2);
    BCase(EF_MIPS_32BITMODE);
    BCase(EF_MIPS_FP64);
    BCase(EF_MIPS_NAN2008);
    BCase(EF_MIPS_MICROMIPS);
    BCase(EF_MIPS_ARCH_ASE_M16);
    BCase(EF_MIPS_ARCH_ASE_MDMX);
    BCaseMask(EF_MIPS_ABI_O32, EF_MIPS_ABI);
    BCaseMask(EF_MIPS_ABI_O64, EF_MIPS_ABI);
    BCaseMask(EF_MIPS_ABI_EABI32, EF_MIPS_ABI);
    BCaseMask(EF_MIPS_ABI_EABI64, EF_MIPS_ABI);
    BCaseMask(EF_MIPS_MACH_3900, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_4010, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_4100, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_4650, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_4120, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_4111, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_SB1, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_OCTEON, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_XLR, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_OCTEON2, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_OCTEON3, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_5400, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_5900, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_5500, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_9000, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_LS2E, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_LS2F, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_MACH_LS3A, EF_MIPS_MACH);
    BCaseMask(EF_MIPS_ARCH_1, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_2, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_3, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_4, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_5, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_32, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_64, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_32R2, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_64R2, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_32R6, EF_MIPS_ARCH);
    BCaseMask(EF_MIPS_ARCH_64R6, EF_MIPS_ARCH);
    break;
  case ELF::EM_HEXAGON:
    BCase(EF_HEXAGON_MACH_V2);
    BCase(EF_HEXAGON_MACH_V3);
    BCase(EF_HEXAGON_MACH_V4);
    BCase(EF_HEXAGON_MACH_V5);
    BCase(EF_HEXAGON_MACH_V55);
    BCase(EF_HEXAGON_MACH_V60);
    BCase(EF_HEXAGON_MACH_V62);
    BCase(EF_HEXAGON_MACH_V65);
    BCase(EF_HEXAGON_ISA_V2);
    BCase(EF_HEXAGON_ISA_V3);
    BCase(EF_HEXAGON_ISA_V4);
    BCase(EF_HEXAGON_ISA_V5);
    BCase(EF_HEXAGON_ISA_V55);
    BCase(EF_HEXAGON_ISA_V60);
    BCase(EF_HEXAGON_ISA_V62);
    BCase(EF_HEXAGON_ISA_V65);
    break;
  case ELF::EM_AVR:
    BCase(EF_AVR_ARCH_AVR1);
    BCase(EF_AVR_ARCH_AVR2);
    BCase(EF_AVR_ARCH_AVR25);
    BCase(EF_AVR_ARCH_AVR3);
    BCase(EF_AVR_ARCH_AVR31);
    BCase(EF_AVR_ARCH_AVR35);
    BCase(EF_AVR_ARCH_AVR4);
    BCase(EF_AVR_ARCH_AVR51);
    BCase(EF_AVR_ARCH_AVR6);
    BCase(EF_AVR_ARCH_AVRTINY);
    BCase(EF_AVR_ARCH_XMEGA1);
    BCase(EF_AVR_ARCH_XMEGA2);
    BCase(EF_AVR_ARCH_XMEGA3);
    BCase(EF_AVR_ARCH_XMEGA4);
    BCase(EF_AVR_ARCH_XMEGA5);
    BCase(EF_AVR_ARCH_XMEGA6);
    BCase(EF_AVR_ARCH_XMEGA7);
    break;
  case ELF::EM_RISCV:
    BCase(EF_RISCV_RVC);
    BCaseMask(EF_RISCV_FLOAT_ABI_SOFT, EF_RISCV_FLOAT_ABI);
    BCaseMask(EF_RISCV_FLOAT_ABI_SINGLE, EF_RISCV_FLOAT_ABI);
    BCaseMask(EF_RISCV_FLOAT_ABI_DOUBLE, EF_RISCV_FLOAT_ABI);
    BCaseMask(EF_RISCV_FLOAT_ABI_QUAD, EF_RISCV_FLOAT_ABI);
    BCase(EF_RISCV_RVE);
    break;
  case ELF::EM_AMDGPU:
    BCaseMask(EF_AMDGPU_MACH_NONE, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_R600, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_R630, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_RS880, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_RV670, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_RV710, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_RV730, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_RV770, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_CEDAR, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_CYPRESS, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_JUNIPER, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_REDWOOD, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_SUMO, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_BARTS, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_CAICOS, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_CAYMAN, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_R600_TURKS, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX600, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX601, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX700, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX701, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX702, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX703, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX704, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX801, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX802, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX803, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX810, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX900, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX902, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX904, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX906, EF_AMDGPU_MACH);
    BCaseMask(EF_AMDGPU_MACH_AMDGCN_GFX909, EF_AMDGPU_MACH);
    BCase(EF_AMDGPU_XNACK);
    BCase(EF_AMDGPU_SRAM_ECC);
    break;
  case ELF::EM_X86_64:
    break;
  default:
    llvm_unreachable("Unsupported architecture");
  }
#undef BCase
#undef BCaseMask
}

void ScalarEnumerationTraits<ELFYAML::ELF_SHT>::enumeration(
    IO &IO, ELFYAML::ELF_SHT &Value) {
  const auto *Object = static_cast<ELFYAML::Object *>(IO.getContext());
  assert(Object && "The IO context is not initialized");
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(SHT_NULL);
  ECase(SHT_PROGBITS);
  ECase(SHT_SYMTAB);
  // FIXME: Issue a diagnostic with this information.
  ECase(SHT_STRTAB);
  ECase(SHT_RELA);
  ECase(SHT_HASH);
  ECase(SHT_DYNAMIC);
  ECase(SHT_NOTE);
  ECase(SHT_NOBITS);
  ECase(SHT_REL);
  ECase(SHT_SHLIB);
  ECase(SHT_DYNSYM);
  ECase(SHT_INIT_ARRAY);
  ECase(SHT_FINI_ARRAY);
  ECase(SHT_PREINIT_ARRAY);
  ECase(SHT_GROUP);
  ECase(SHT_SYMTAB_SHNDX);
  ECase(SHT_RELR);
  ECase(SHT_LOOS);
  ECase(SHT_ANDROID_REL);
  ECase(SHT_ANDROID_RELA);
  ECase(SHT_ANDROID_RELR);
  ECase(SHT_LLVM_ODRTAB);
  ECase(SHT_LLVM_LINKER_OPTIONS);
  ECase(SHT_LLVM_CALL_GRAPH_PROFILE);
  ECase(SHT_LLVM_ADDRSIG);
  ECase(SHT_GNU_ATTRIBUTES);
  ECase(SHT_GNU_HASH);
  ECase(SHT_GNU_verdef);
  ECase(SHT_GNU_verneed);
  ECase(SHT_GNU_versym);
  ECase(SHT_HIOS);
  ECase(SHT_LOPROC);
  switch (Object->Header.Machine) {
  case ELF::EM_ARM:
    ECase(SHT_ARM_EXIDX);
    ECase(SHT_ARM_PREEMPTMAP);
    ECase(SHT_ARM_ATTRIBUTES);
    ECase(SHT_ARM_DEBUGOVERLAY);
    ECase(SHT_ARM_OVERLAYSECTION);
    break;
  case ELF::EM_HEXAGON:
    ECase(SHT_HEX_ORDERED);
    break;
  case ELF::EM_X86_64:
    ECase(SHT_X86_64_UNWIND);
    break;
  case ELF::EM_MIPS:
    ECase(SHT_MIPS_REGINFO);
    ECase(SHT_MIPS_OPTIONS);
    ECase(SHT_MIPS_ABIFLAGS);
    break;
  default:
    // Nothing to do.
    break;
  }
#undef ECase
}

void ScalarBitSetTraits<ELFYAML::ELF_PF>::bitset(IO &IO,
                                                 ELFYAML::ELF_PF &Value) {
#define BCase(X) IO.bitSetCase(Value, #X, ELF::X)
  BCase(PF_X);
  BCase(PF_W);
  BCase(PF_R);
}

void ScalarBitSetTraits<ELFYAML::ELF_SHF>::bitset(IO &IO,
                                                  ELFYAML::ELF_SHF &Value) {
  const auto *Object = static_cast<ELFYAML::Object *>(IO.getContext());
#define BCase(X) IO.bitSetCase(Value, #X, ELF::X)
  BCase(SHF_WRITE);
  BCase(SHF_ALLOC);
  BCase(SHF_EXCLUDE);
  BCase(SHF_EXECINSTR);
  BCase(SHF_MERGE);
  BCase(SHF_STRINGS);
  BCase(SHF_INFO_LINK);
  BCase(SHF_LINK_ORDER);
  BCase(SHF_OS_NONCONFORMING);
  BCase(SHF_GROUP);
  BCase(SHF_TLS);
  BCase(SHF_COMPRESSED);
  switch (Object->Header.Machine) {
  case ELF::EM_ARM:
    BCase(SHF_ARM_PURECODE);
    break;
  case ELF::EM_HEXAGON:
    BCase(SHF_HEX_GPREL);
    break;
  case ELF::EM_MIPS:
    BCase(SHF_MIPS_NODUPES);
    BCase(SHF_MIPS_NAMES);
    BCase(SHF_MIPS_LOCAL);
    BCase(SHF_MIPS_NOSTRIP);
    BCase(SHF_MIPS_GPREL);
    BCase(SHF_MIPS_MERGE);
    BCase(SHF_MIPS_ADDR);
    BCase(SHF_MIPS_STRING);
    break;
  case ELF::EM_X86_64:
    BCase(SHF_X86_64_LARGE);
    break;
  default:
    // Nothing to do.
    break;
  }
#undef BCase
}

void ScalarEnumerationTraits<ELFYAML::ELF_SHN>::enumeration(
    IO &IO, ELFYAML::ELF_SHN &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(SHN_UNDEF);
  ECase(SHN_LORESERVE);
  ECase(SHN_LOPROC);
  ECase(SHN_HIPROC);
  ECase(SHN_LOOS);
  ECase(SHN_HIOS);
  ECase(SHN_ABS);
  ECase(SHN_COMMON);
  ECase(SHN_XINDEX);
  ECase(SHN_HIRESERVE);
  ECase(SHN_HEXAGON_SCOMMON);
  ECase(SHN_HEXAGON_SCOMMON_1);
  ECase(SHN_HEXAGON_SCOMMON_2);
  ECase(SHN_HEXAGON_SCOMMON_4);
  ECase(SHN_HEXAGON_SCOMMON_8);
#undef ECase
  IO.enumFallback<Hex32>(Value);
}

void ScalarEnumerationTraits<ELFYAML::ELF_STT>::enumeration(
    IO &IO, ELFYAML::ELF_STT &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(STT_NOTYPE);
  ECase(STT_OBJECT);
  ECase(STT_FUNC);
  ECase(STT_SECTION);
  ECase(STT_FILE);
  ECase(STT_COMMON);
  ECase(STT_TLS);
  ECase(STT_GNU_IFUNC);
#undef ECase
}

void ScalarEnumerationTraits<ELFYAML::ELF_STV>::enumeration(
    IO &IO, ELFYAML::ELF_STV &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(STV_DEFAULT);
  ECase(STV_INTERNAL);
  ECase(STV_HIDDEN);
  ECase(STV_PROTECTED);
#undef ECase
}

void ScalarBitSetTraits<ELFYAML::ELF_STO>::bitset(IO &IO,
                                                  ELFYAML::ELF_STO &Value) {
  const auto *Object = static_cast<ELFYAML::Object *>(IO.getContext());
  assert(Object && "The IO context is not initialized");
#define BCase(X) IO.bitSetCase(Value, #X, ELF::X)
  switch (Object->Header.Machine) {
  case ELF::EM_MIPS:
    BCase(STO_MIPS_OPTIONAL);
    BCase(STO_MIPS_PLT);
    BCase(STO_MIPS_PIC);
    BCase(STO_MIPS_MICROMIPS);
    break;
  default:
    break; // Nothing to do
  }
#undef BCase
#undef BCaseMask
}

void ScalarEnumerationTraits<ELFYAML::ELF_RSS>::enumeration(
    IO &IO, ELFYAML::ELF_RSS &Value) {
#define ECase(X) IO.enumCase(Value, #X, ELF::X)
  ECase(RSS_UNDEF);
  ECase(RSS_GP);
  ECase(RSS_GP0);
  ECase(RSS_LOC);
#undef ECase
}

void ScalarEnumerationTraits<ELFYAML::ELF_REL>::enumeration(
    IO &IO, ELFYAML::ELF_REL &Value) {
  const auto *Object = static_cast<ELFYAML::Object *>(IO.getContext());
  assert(Object && "The IO context is not initialized");
#define ELF_RELOC(X, Y) IO.enumCase(Value, #X, ELF::X);
  switch (Object->Header.Machine) {
  case ELF::EM_X86_64:
#include "llvm/BinaryFormat/ELFRelocs/x86_64.def"
    break;
  case ELF::EM_MIPS:
#include "llvm/BinaryFormat/ELFRelocs/Mips.def"
    break;
  case ELF::EM_HEXAGON:
#include "llvm/BinaryFormat/ELFRelocs/Hexagon.def"
    break;
  case ELF::EM_386:
  case ELF::EM_IAMCU:
#include "llvm/BinaryFormat/ELFRelocs/i386.def"
    break;
  case ELF::EM_AARCH64:
#include "llvm/BinaryFormat/ELFRelocs/AArch64.def"
    break;
  case ELF::EM_ARM:
#include "llvm/BinaryFormat/ELFRelocs/ARM.def"
    break;
  case ELF::EM_ARC:
#include "llvm/BinaryFormat/ELFRelocs/ARC.def"
    break;
  case ELF::EM_RISCV:
#include "llvm/BinaryFormat/ELFRelocs/RISCV.def"
    break;
  case ELF::EM_LANAI:
#include "llvm/BinaryFormat/ELFRelocs/Lanai.def"
    break;
  case ELF::EM_AMDGPU:
#include "llvm/BinaryFormat/ELFRelocs/AMDGPU.def"
    break;
  case ELF::EM_BPF:
#include "llvm/BinaryFormat/ELFRelocs/BPF.def"
    break;
  default:
    llvm_unreachable("Unsupported architecture");
  }
#undef ELF_RELOC
  IO.enumFallback<Hex32>(Value);
}

void ScalarEnumerationTraits<ELFYAML::MIPS_AFL_REG>::enumeration(
    IO &IO, ELFYAML::MIPS_AFL_REG &Value) {
#define ECase(X) IO.enumCase(Value, #X, Mips::AFL_##X)
  ECase(REG_NONE);
  ECase(REG_32);
  ECase(REG_64);
  ECase(REG_128);
#undef ECase
}

void ScalarEnumerationTraits<ELFYAML::MIPS_ABI_FP>::enumeration(
    IO &IO, ELFYAML::MIPS_ABI_FP &Value) {
#define ECase(X) IO.enumCase(Value, #X, Mips::Val_GNU_MIPS_ABI_##X)
  ECase(FP_ANY);
  ECase(FP_DOUBLE);
  ECase(FP_SINGLE);
  ECase(FP_SOFT);
  ECase(FP_OLD_64);
  ECase(FP_XX);
  ECase(FP_64);
  ECase(FP_64A);
#undef ECase
}

void ScalarEnumerationTraits<ELFYAML::MIPS_AFL_EXT>::enumeration(
    IO &IO, ELFYAML::MIPS_AFL_EXT &Value) {
#define ECase(X) IO.enumCase(Value, #X, Mips::AFL_##X)
  ECase(EXT_NONE);
  ECase(EXT_XLR);
  ECase(EXT_OCTEON2);
  ECase(EXT_OCTEONP);
  ECase(EXT_LOONGSON_3A);
  ECase(EXT_OCTEON);
  ECase(EXT_5900);
  ECase(EXT_4650);
  ECase(EXT_4010);
  ECase(EXT_4100);
  ECase(EXT_3900);
  ECase(EXT_10000);
  ECase(EXT_SB1);
  ECase(EXT_4111);
  ECase(EXT_4120);
  ECase(EXT_5400);
  ECase(EXT_5500);
  ECase(EXT_LOONGSON_2E);
  ECase(EXT_LOONGSON_2F);
  ECase(EXT_OCTEON3);
#undef ECase
}

void ScalarEnumerationTraits<ELFYAML::MIPS_ISA>::enumeration(
    IO &IO, ELFYAML::MIPS_ISA &Value) {
  IO.enumCase(Value, "MIPS1", 1);
  IO.enumCase(Value, "MIPS2", 2);
  IO.enumCase(Value, "MIPS3", 3);
  IO.enumCase(Value, "MIPS4", 4);
  IO.enumCase(Value, "MIPS5", 5);
  IO.enumCase(Value, "MIPS32", 32);
  IO.enumCase(Value, "MIPS64", 64);
}

void ScalarBitSetTraits<ELFYAML::MIPS_AFL_ASE>::bitset(
    IO &IO, ELFYAML::MIPS_AFL_ASE &Value) {
#define BCase(X) IO.bitSetCase(Value, #X, Mips::AFL_ASE_##X)
  BCase(DSP);
  BCase(DSPR2);
  BCase(EVA);
  BCase(MCU);
  BCase(MDMX);
  BCase(MIPS3D);
  BCase(MT);
  BCase(SMARTMIPS);
  BCase(VIRT);
  BCase(MSA);
  BCase(MIPS16);
  BCase(MICROMIPS);
  BCase(XPA);
#undef BCase
}

void ScalarBitSetTraits<ELFYAML::MIPS_AFL_FLAGS1>::bitset(
    IO &IO, ELFYAML::MIPS_AFL_FLAGS1 &Value) {
#define BCase(X) IO.bitSetCase(Value, #X, Mips::AFL_FLAGS1_##X)
  BCase(ODDSPREG);
#undef BCase
}

void MappingTraits<ELFYAML::FileHeader>::mapping(IO &IO,
                                                 ELFYAML::FileHeader &FileHdr) {
  IO.mapRequired("Class", FileHdr.Class);
  IO.mapRequired("Data", FileHdr.Data);
  IO.mapOptional("OSABI", FileHdr.OSABI, ELFYAML::ELF_ELFOSABI(0));
  IO.mapOptional("ABIVersion", FileHdr.ABIVersion, Hex8(0));
  IO.mapRequired("Type", FileHdr.Type);
  IO.mapRequired("Machine", FileHdr.Machine);
  IO.mapOptional("Flags", FileHdr.Flags, ELFYAML::ELF_EF(0));
  IO.mapOptional("Entry", FileHdr.Entry, Hex64(0));
}

void MappingTraits<ELFYAML::ProgramHeader>::mapping(
    IO &IO, ELFYAML::ProgramHeader &Phdr) {
  IO.mapRequired("Type", Phdr.Type);
  IO.mapOptional("Flags", Phdr.Flags, ELFYAML::ELF_PF(0));
  IO.mapOptional("Sections", Phdr.Sections);
  IO.mapOptional("VAddr", Phdr.VAddr, Hex64(0));
  IO.mapOptional("PAddr", Phdr.PAddr, Hex64(0));
  IO.mapOptional("Align", Phdr.Align);
}

namespace {

struct NormalizedOther {
  NormalizedOther(IO &)
      : Visibility(ELFYAML::ELF_STV(0)), Other(ELFYAML::ELF_STO(0)) {}
  NormalizedOther(IO &, uint8_t Original)
      : Visibility(Original & 0x3), Other(Original & ~0x3) {}

  uint8_t denormalize(IO &) { return Visibility | Other; }

  ELFYAML::ELF_STV Visibility;
  ELFYAML::ELF_STO Other;
};

} // end anonymous namespace

void MappingTraits<ELFYAML::Symbol>::mapping(IO &IO, ELFYAML::Symbol &Symbol) {
  IO.mapOptional("Name", Symbol.Name, StringRef());
  IO.mapOptional("Type", Symbol.Type, ELFYAML::ELF_STT(0));
  IO.mapOptional("Section", Symbol.Section, StringRef());
  IO.mapOptional("Index", Symbol.Index);
  IO.mapOptional("Value", Symbol.Value, Hex64(0));
  IO.mapOptional("Size", Symbol.Size, Hex64(0));

  MappingNormalization<NormalizedOther, uint8_t> Keys(IO, Symbol.Other);
  IO.mapOptional("Visibility", Keys->Visibility, ELFYAML::ELF_STV(0));
  IO.mapOptional("Other", Keys->Other, ELFYAML::ELF_STO(0));
}

StringRef MappingTraits<ELFYAML::Symbol>::validate(IO &IO,
                                                   ELFYAML::Symbol &Symbol) {
  if (Symbol.Index && Symbol.Section.data()) {
    return "Index and Section cannot both be specified for Symbol";
  }
  if (Symbol.Index && *Symbol.Index == ELFYAML::ELF_SHN(ELF::SHN_XINDEX)) {
    return "Large indexes are not supported";
  }
  if (Symbol.Index && *Symbol.Index < ELFYAML::ELF_SHN(ELF::SHN_LORESERVE)) {
    return "Use a section name to define which section a symbol is defined in";
  }
  return StringRef();
}

void MappingTraits<ELFYAML::LocalGlobalWeakSymbols>::mapping(
    IO &IO, ELFYAML::LocalGlobalWeakSymbols &Symbols) {
  IO.mapOptional("Local", Symbols.Local);
  IO.mapOptional("Global", Symbols.Global);
  IO.mapOptional("Weak", Symbols.Weak);
}

static void commonSectionMapping(IO &IO, ELFYAML::Section &Section) {
  IO.mapOptional("Name", Section.Name, StringRef());
  IO.mapRequired("Type", Section.Type);
  IO.mapOptional("Flags", Section.Flags, ELFYAML::ELF_SHF(0));
  IO.mapOptional("Address", Section.Address, Hex64(0));
  IO.mapOptional("Link", Section.Link, StringRef());
  IO.mapOptional("AddressAlign", Section.AddressAlign, Hex64(0));
  IO.mapOptional("EntSize", Section.EntSize);
  IO.mapOptional("Info", Section.Info, StringRef());
}

static void sectionMapping(IO &IO, ELFYAML::RawContentSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Content", Section.Content);
  IO.mapOptional("Size", Section.Size, Hex64(Section.Content.binary_size()));
}

static void sectionMapping(IO &IO, ELFYAML::NoBitsSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Size", Section.Size, Hex64(0));
}

static void sectionMapping(IO &IO, ELFYAML::RelocationSection &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Relocations", Section.Relocations);
}

static void groupSectionMapping(IO &IO, ELFYAML::Group &group) {
  commonSectionMapping(IO, group);
  IO.mapRequired("Members", group.Members);
}

void MappingTraits<ELFYAML::SectionOrType>::mapping(
    IO &IO, ELFYAML::SectionOrType &sectionOrType) {
  IO.mapRequired("SectionOrType", sectionOrType.sectionNameOrType);
}

void MappingTraits<ELFYAML::SectionName>::mapping(
    IO &IO, ELFYAML::SectionName &sectionName) {
  IO.mapRequired("Section", sectionName.Section);
}

static void sectionMapping(IO &IO, ELFYAML::MipsABIFlags &Section) {
  commonSectionMapping(IO, Section);
  IO.mapOptional("Version", Section.Version, Hex16(0));
  IO.mapRequired("ISA", Section.ISALevel);
  IO.mapOptional("ISARevision", Section.ISARevision, Hex8(0));
  IO.mapOptional("ISAExtension", Section.ISAExtension,
                 ELFYAML::MIPS_AFL_EXT(Mips::AFL_EXT_NONE));
  IO.mapOptional("ASEs", Section.ASEs, ELFYAML::MIPS_AFL_ASE(0));
  IO.mapOptional("FpABI", Section.FpABI,
                 ELFYAML::MIPS_ABI_FP(Mips::Val_GNU_MIPS_ABI_FP_ANY));
  IO.mapOptional("GPRSize", Section.GPRSize,
                 ELFYAML::MIPS_AFL_REG(Mips::AFL_REG_NONE));
  IO.mapOptional("CPR1Size", Section.CPR1Size,
                 ELFYAML::MIPS_AFL_REG(Mips::AFL_REG_NONE));
  IO.mapOptional("CPR2Size", Section.CPR2Size,
                 ELFYAML::MIPS_AFL_REG(Mips::AFL_REG_NONE));
  IO.mapOptional("Flags1", Section.Flags1, ELFYAML::MIPS_AFL_FLAGS1(0));
  IO.mapOptional("Flags2", Section.Flags2, Hex32(0));
}

void MappingTraits<std::unique_ptr<ELFYAML::Section>>::mapping(
    IO &IO, std::unique_ptr<ELFYAML::Section> &Section) {
  ELFYAML::ELF_SHT sectionType;
  if (IO.outputting())
    sectionType = Section->Type;
  else
    IO.mapRequired("Type", sectionType);

  switch (sectionType) {
  case ELF::SHT_REL:
  case ELF::SHT_RELA:
    if (!IO.outputting())
      Section.reset(new ELFYAML::RelocationSection());
    sectionMapping(IO, *cast<ELFYAML::RelocationSection>(Section.get()));
    break;
  case ELF::SHT_GROUP:
    if (!IO.outputting())
      Section.reset(new ELFYAML::Group());
    groupSectionMapping(IO, *cast<ELFYAML::Group>(Section.get()));
    break;
  case ELF::SHT_NOBITS:
    if (!IO.outputting())
      Section.reset(new ELFYAML::NoBitsSection());
    sectionMapping(IO, *cast<ELFYAML::NoBitsSection>(Section.get()));
    break;
  case ELF::SHT_MIPS_ABIFLAGS:
    if (!IO.outputting())
      Section.reset(new ELFYAML::MipsABIFlags());
    sectionMapping(IO, *cast<ELFYAML::MipsABIFlags>(Section.get()));
    break;
  default:
    if (!IO.outputting())
      Section.reset(new ELFYAML::RawContentSection());
    sectionMapping(IO, *cast<ELFYAML::RawContentSection>(Section.get()));
  }
}

StringRef MappingTraits<std::unique_ptr<ELFYAML::Section>>::validate(
    IO &io, std::unique_ptr<ELFYAML::Section> &Section) {
  const auto *RawSection = dyn_cast<ELFYAML::RawContentSection>(Section.get());
  if (!RawSection || RawSection->Size >= RawSection->Content.binary_size())
    return StringRef();
  return "Section size must be greater or equal to the content size";
}

namespace {

struct NormalizedMips64RelType {
  NormalizedMips64RelType(IO &)
      : Type(ELFYAML::ELF_REL(ELF::R_MIPS_NONE)),
        Type2(ELFYAML::ELF_REL(ELF::R_MIPS_NONE)),
        Type3(ELFYAML::ELF_REL(ELF::R_MIPS_NONE)),
        SpecSym(ELFYAML::ELF_REL(ELF::RSS_UNDEF)) {}
  NormalizedMips64RelType(IO &, ELFYAML::ELF_REL Original)
      : Type(Original & 0xFF), Type2(Original >> 8 & 0xFF),
        Type3(Original >> 16 & 0xFF), SpecSym(Original >> 24 & 0xFF) {}

  ELFYAML::ELF_REL denormalize(IO &) {
    ELFYAML::ELF_REL Res = Type | Type2 << 8 | Type3 << 16 | SpecSym << 24;
    return Res;
  }

  ELFYAML::ELF_REL Type;
  ELFYAML::ELF_REL Type2;
  ELFYAML::ELF_REL Type3;
  ELFYAML::ELF_RSS SpecSym;
};

} // end anonymous namespace

void MappingTraits<ELFYAML::Relocation>::mapping(IO &IO,
                                                 ELFYAML::Relocation &Rel) {
  const auto *Object = static_cast<ELFYAML::Object *>(IO.getContext());
  assert(Object && "The IO context is not initialized");

  IO.mapRequired("Offset", Rel.Offset);
  IO.mapOptional("Symbol", Rel.Symbol);

  if (Object->Header.Machine == ELFYAML::ELF_EM(ELF::EM_MIPS) &&
      Object->Header.Class == ELFYAML::ELF_ELFCLASS(ELF::ELFCLASS64)) {
    MappingNormalization<NormalizedMips64RelType, ELFYAML::ELF_REL> Key(
        IO, Rel.Type);
    IO.mapRequired("Type", Key->Type);
    IO.mapOptional("Type2", Key->Type2, ELFYAML::ELF_REL(ELF::R_MIPS_NONE));
    IO.mapOptional("Type3", Key->Type3, ELFYAML::ELF_REL(ELF::R_MIPS_NONE));
    IO.mapOptional("SpecSym", Key->SpecSym, ELFYAML::ELF_RSS(ELF::RSS_UNDEF));
  } else
    IO.mapRequired("Type", Rel.Type);

  IO.mapOptional("Addend", Rel.Addend, (int64_t)0);
}

void MappingTraits<ELFYAML::Object>::mapping(IO &IO, ELFYAML::Object &Object) {
  assert(!IO.getContext() && "The IO context is initialized already");
  IO.setContext(&Object);
  IO.mapTag("!ELF", true);
  IO.mapRequired("FileHeader", Object.Header);
  IO.mapOptional("ProgramHeaders", Object.ProgramHeaders);
  IO.mapOptional("Sections", Object.Sections);
  IO.mapOptional("Symbols", Object.Symbols);
  IO.mapOptional("DynamicSymbols", Object.DynamicSymbols);
  IO.setContext(nullptr);
}

LLVM_YAML_STRONG_TYPEDEF(uint8_t, MIPS_AFL_REG)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, MIPS_ABI_FP)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, MIPS_AFL_EXT)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, MIPS_AFL_ASE)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, MIPS_AFL_FLAGS1)

} // end namespace yaml

} // end namespace llvm
