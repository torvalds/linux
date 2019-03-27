//===-- EmulateInstructionARM.cpp -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdlib.h>

#include "EmulateInstructionARM.h"
#include "EmulationStateARM.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Interpreter/OptionValueArray.h"
#include "lldb/Interpreter/OptionValueDictionary.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"

#include "Plugins/Process/Utility/ARMDefines.h"
#include "Plugins/Process/Utility/ARMUtils.h"
#include "Utility/ARM_DWARF_Registers.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/MathExtras.h"

using namespace lldb;
using namespace lldb_private;

// Convenient macro definitions.
#define APSR_C Bit32(m_opcode_cpsr, CPSR_C_POS)
#define APSR_V Bit32(m_opcode_cpsr, CPSR_V_POS)

#define AlignPC(pc_val) (pc_val & 0xFFFFFFFC)

//----------------------------------------------------------------------
//
// ITSession implementation
//
//----------------------------------------------------------------------

static bool GetARMDWARFRegisterInfo(unsigned reg_num, RegisterInfo &reg_info) {
  ::memset(&reg_info, 0, sizeof(RegisterInfo));
  ::memset(reg_info.kinds, LLDB_INVALID_REGNUM, sizeof(reg_info.kinds));

  if (reg_num >= dwarf_q0 && reg_num <= dwarf_q15) {
    reg_info.byte_size = 16;
    reg_info.format = eFormatVectorOfUInt8;
    reg_info.encoding = eEncodingVector;
  }

  if (reg_num >= dwarf_d0 && reg_num <= dwarf_d31) {
    reg_info.byte_size = 8;
    reg_info.format = eFormatFloat;
    reg_info.encoding = eEncodingIEEE754;
  } else if (reg_num >= dwarf_s0 && reg_num <= dwarf_s31) {
    reg_info.byte_size = 4;
    reg_info.format = eFormatFloat;
    reg_info.encoding = eEncodingIEEE754;
  } else if (reg_num >= dwarf_f0 && reg_num <= dwarf_f7) {
    reg_info.byte_size = 12;
    reg_info.format = eFormatFloat;
    reg_info.encoding = eEncodingIEEE754;
  } else {
    reg_info.byte_size = 4;
    reg_info.format = eFormatHex;
    reg_info.encoding = eEncodingUint;
  }

  reg_info.kinds[eRegisterKindDWARF] = reg_num;

  switch (reg_num) {
  case dwarf_r0:
    reg_info.name = "r0";
    break;
  case dwarf_r1:
    reg_info.name = "r1";
    break;
  case dwarf_r2:
    reg_info.name = "r2";
    break;
  case dwarf_r3:
    reg_info.name = "r3";
    break;
  case dwarf_r4:
    reg_info.name = "r4";
    break;
  case dwarf_r5:
    reg_info.name = "r5";
    break;
  case dwarf_r6:
    reg_info.name = "r6";
    break;
  case dwarf_r7:
    reg_info.name = "r7";
    reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FP;
    break;
  case dwarf_r8:
    reg_info.name = "r8";
    break;
  case dwarf_r9:
    reg_info.name = "r9";
    break;
  case dwarf_r10:
    reg_info.name = "r10";
    break;
  case dwarf_r11:
    reg_info.name = "r11";
    break;
  case dwarf_r12:
    reg_info.name = "r12";
    break;
  case dwarf_sp:
    reg_info.name = "sp";
    reg_info.alt_name = "r13";
    reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_SP;
    break;
  case dwarf_lr:
    reg_info.name = "lr";
    reg_info.alt_name = "r14";
    reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_RA;
    break;
  case dwarf_pc:
    reg_info.name = "pc";
    reg_info.alt_name = "r15";
    reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
    break;
  case dwarf_cpsr:
    reg_info.name = "cpsr";
    reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_FLAGS;
    break;

  case dwarf_s0:
    reg_info.name = "s0";
    break;
  case dwarf_s1:
    reg_info.name = "s1";
    break;
  case dwarf_s2:
    reg_info.name = "s2";
    break;
  case dwarf_s3:
    reg_info.name = "s3";
    break;
  case dwarf_s4:
    reg_info.name = "s4";
    break;
  case dwarf_s5:
    reg_info.name = "s5";
    break;
  case dwarf_s6:
    reg_info.name = "s6";
    break;
  case dwarf_s7:
    reg_info.name = "s7";
    break;
  case dwarf_s8:
    reg_info.name = "s8";
    break;
  case dwarf_s9:
    reg_info.name = "s9";
    break;
  case dwarf_s10:
    reg_info.name = "s10";
    break;
  case dwarf_s11:
    reg_info.name = "s11";
    break;
  case dwarf_s12:
    reg_info.name = "s12";
    break;
  case dwarf_s13:
    reg_info.name = "s13";
    break;
  case dwarf_s14:
    reg_info.name = "s14";
    break;
  case dwarf_s15:
    reg_info.name = "s15";
    break;
  case dwarf_s16:
    reg_info.name = "s16";
    break;
  case dwarf_s17:
    reg_info.name = "s17";
    break;
  case dwarf_s18:
    reg_info.name = "s18";
    break;
  case dwarf_s19:
    reg_info.name = "s19";
    break;
  case dwarf_s20:
    reg_info.name = "s20";
    break;
  case dwarf_s21:
    reg_info.name = "s21";
    break;
  case dwarf_s22:
    reg_info.name = "s22";
    break;
  case dwarf_s23:
    reg_info.name = "s23";
    break;
  case dwarf_s24:
    reg_info.name = "s24";
    break;
  case dwarf_s25:
    reg_info.name = "s25";
    break;
  case dwarf_s26:
    reg_info.name = "s26";
    break;
  case dwarf_s27:
    reg_info.name = "s27";
    break;
  case dwarf_s28:
    reg_info.name = "s28";
    break;
  case dwarf_s29:
    reg_info.name = "s29";
    break;
  case dwarf_s30:
    reg_info.name = "s30";
    break;
  case dwarf_s31:
    reg_info.name = "s31";
    break;

  // FPA Registers 0-7
  case dwarf_f0:
    reg_info.name = "f0";
    break;
  case dwarf_f1:
    reg_info.name = "f1";
    break;
  case dwarf_f2:
    reg_info.name = "f2";
    break;
  case dwarf_f3:
    reg_info.name = "f3";
    break;
  case dwarf_f4:
    reg_info.name = "f4";
    break;
  case dwarf_f5:
    reg_info.name = "f5";
    break;
  case dwarf_f6:
    reg_info.name = "f6";
    break;
  case dwarf_f7:
    reg_info.name = "f7";
    break;

  // Intel wireless MMX general purpose registers 0 - 7 XScale accumulator
  // register 0 - 7 (they do overlap with wCGR0 - wCGR7)
  case dwarf_wCGR0:
    reg_info.name = "wCGR0/ACC0";
    break;
  case dwarf_wCGR1:
    reg_info.name = "wCGR1/ACC1";
    break;
  case dwarf_wCGR2:
    reg_info.name = "wCGR2/ACC2";
    break;
  case dwarf_wCGR3:
    reg_info.name = "wCGR3/ACC3";
    break;
  case dwarf_wCGR4:
    reg_info.name = "wCGR4/ACC4";
    break;
  case dwarf_wCGR5:
    reg_info.name = "wCGR5/ACC5";
    break;
  case dwarf_wCGR6:
    reg_info.name = "wCGR6/ACC6";
    break;
  case dwarf_wCGR7:
    reg_info.name = "wCGR7/ACC7";
    break;

  // Intel wireless MMX data registers 0 - 15
  case dwarf_wR0:
    reg_info.name = "wR0";
    break;
  case dwarf_wR1:
    reg_info.name = "wR1";
    break;
  case dwarf_wR2:
    reg_info.name = "wR2";
    break;
  case dwarf_wR3:
    reg_info.name = "wR3";
    break;
  case dwarf_wR4:
    reg_info.name = "wR4";
    break;
  case dwarf_wR5:
    reg_info.name = "wR5";
    break;
  case dwarf_wR6:
    reg_info.name = "wR6";
    break;
  case dwarf_wR7:
    reg_info.name = "wR7";
    break;
  case dwarf_wR8:
    reg_info.name = "wR8";
    break;
  case dwarf_wR9:
    reg_info.name = "wR9";
    break;
  case dwarf_wR10:
    reg_info.name = "wR10";
    break;
  case dwarf_wR11:
    reg_info.name = "wR11";
    break;
  case dwarf_wR12:
    reg_info.name = "wR12";
    break;
  case dwarf_wR13:
    reg_info.name = "wR13";
    break;
  case dwarf_wR14:
    reg_info.name = "wR14";
    break;
  case dwarf_wR15:
    reg_info.name = "wR15";
    break;

  case dwarf_spsr:
    reg_info.name = "spsr";
    break;
  case dwarf_spsr_fiq:
    reg_info.name = "spsr_fiq";
    break;
  case dwarf_spsr_irq:
    reg_info.name = "spsr_irq";
    break;
  case dwarf_spsr_abt:
    reg_info.name = "spsr_abt";
    break;
  case dwarf_spsr_und:
    reg_info.name = "spsr_und";
    break;
  case dwarf_spsr_svc:
    reg_info.name = "spsr_svc";
    break;

  case dwarf_r8_usr:
    reg_info.name = "r8_usr";
    break;
  case dwarf_r9_usr:
    reg_info.name = "r9_usr";
    break;
  case dwarf_r10_usr:
    reg_info.name = "r10_usr";
    break;
  case dwarf_r11_usr:
    reg_info.name = "r11_usr";
    break;
  case dwarf_r12_usr:
    reg_info.name = "r12_usr";
    break;
  case dwarf_r13_usr:
    reg_info.name = "r13_usr";
    break;
  case dwarf_r14_usr:
    reg_info.name = "r14_usr";
    break;
  case dwarf_r8_fiq:
    reg_info.name = "r8_fiq";
    break;
  case dwarf_r9_fiq:
    reg_info.name = "r9_fiq";
    break;
  case dwarf_r10_fiq:
    reg_info.name = "r10_fiq";
    break;
  case dwarf_r11_fiq:
    reg_info.name = "r11_fiq";
    break;
  case dwarf_r12_fiq:
    reg_info.name = "r12_fiq";
    break;
  case dwarf_r13_fiq:
    reg_info.name = "r13_fiq";
    break;
  case dwarf_r14_fiq:
    reg_info.name = "r14_fiq";
    break;
  case dwarf_r13_irq:
    reg_info.name = "r13_irq";
    break;
  case dwarf_r14_irq:
    reg_info.name = "r14_irq";
    break;
  case dwarf_r13_abt:
    reg_info.name = "r13_abt";
    break;
  case dwarf_r14_abt:
    reg_info.name = "r14_abt";
    break;
  case dwarf_r13_und:
    reg_info.name = "r13_und";
    break;
  case dwarf_r14_und:
    reg_info.name = "r14_und";
    break;
  case dwarf_r13_svc:
    reg_info.name = "r13_svc";
    break;
  case dwarf_r14_svc:
    reg_info.name = "r14_svc";
    break;

  // Intel wireless MMX control register in co-processor 0 - 7
  case dwarf_wC0:
    reg_info.name = "wC0";
    break;
  case dwarf_wC1:
    reg_info.name = "wC1";
    break;
  case dwarf_wC2:
    reg_info.name = "wC2";
    break;
  case dwarf_wC3:
    reg_info.name = "wC3";
    break;
  case dwarf_wC4:
    reg_info.name = "wC4";
    break;
  case dwarf_wC5:
    reg_info.name = "wC5";
    break;
  case dwarf_wC6:
    reg_info.name = "wC6";
    break;
  case dwarf_wC7:
    reg_info.name = "wC7";
    break;

  // VFP-v3/Neon
  case dwarf_d0:
    reg_info.name = "d0";
    break;
  case dwarf_d1:
    reg_info.name = "d1";
    break;
  case dwarf_d2:
    reg_info.name = "d2";
    break;
  case dwarf_d3:
    reg_info.name = "d3";
    break;
  case dwarf_d4:
    reg_info.name = "d4";
    break;
  case dwarf_d5:
    reg_info.name = "d5";
    break;
  case dwarf_d6:
    reg_info.name = "d6";
    break;
  case dwarf_d7:
    reg_info.name = "d7";
    break;
  case dwarf_d8:
    reg_info.name = "d8";
    break;
  case dwarf_d9:
    reg_info.name = "d9";
    break;
  case dwarf_d10:
    reg_info.name = "d10";
    break;
  case dwarf_d11:
    reg_info.name = "d11";
    break;
  case dwarf_d12:
    reg_info.name = "d12";
    break;
  case dwarf_d13:
    reg_info.name = "d13";
    break;
  case dwarf_d14:
    reg_info.name = "d14";
    break;
  case dwarf_d15:
    reg_info.name = "d15";
    break;
  case dwarf_d16:
    reg_info.name = "d16";
    break;
  case dwarf_d17:
    reg_info.name = "d17";
    break;
  case dwarf_d18:
    reg_info.name = "d18";
    break;
  case dwarf_d19:
    reg_info.name = "d19";
    break;
  case dwarf_d20:
    reg_info.name = "d20";
    break;
  case dwarf_d21:
    reg_info.name = "d21";
    break;
  case dwarf_d22:
    reg_info.name = "d22";
    break;
  case dwarf_d23:
    reg_info.name = "d23";
    break;
  case dwarf_d24:
    reg_info.name = "d24";
    break;
  case dwarf_d25:
    reg_info.name = "d25";
    break;
  case dwarf_d26:
    reg_info.name = "d26";
    break;
  case dwarf_d27:
    reg_info.name = "d27";
    break;
  case dwarf_d28:
    reg_info.name = "d28";
    break;
  case dwarf_d29:
    reg_info.name = "d29";
    break;
  case dwarf_d30:
    reg_info.name = "d30";
    break;
  case dwarf_d31:
    reg_info.name = "d31";
    break;

  // NEON 128-bit vector registers (overlays the d registers)
  case dwarf_q0:
    reg_info.name = "q0";
    break;
  case dwarf_q1:
    reg_info.name = "q1";
    break;
  case dwarf_q2:
    reg_info.name = "q2";
    break;
  case dwarf_q3:
    reg_info.name = "q3";
    break;
  case dwarf_q4:
    reg_info.name = "q4";
    break;
  case dwarf_q5:
    reg_info.name = "q5";
    break;
  case dwarf_q6:
    reg_info.name = "q6";
    break;
  case dwarf_q7:
    reg_info.name = "q7";
    break;
  case dwarf_q8:
    reg_info.name = "q8";
    break;
  case dwarf_q9:
    reg_info.name = "q9";
    break;
  case dwarf_q10:
    reg_info.name = "q10";
    break;
  case dwarf_q11:
    reg_info.name = "q11";
    break;
  case dwarf_q12:
    reg_info.name = "q12";
    break;
  case dwarf_q13:
    reg_info.name = "q13";
    break;
  case dwarf_q14:
    reg_info.name = "q14";
    break;
  case dwarf_q15:
    reg_info.name = "q15";
    break;

  default:
    return false;
  }
  return true;
}

// A8.6.50
// Valid return values are {1, 2, 3, 4}, with 0 signifying an error condition.
static uint32_t CountITSize(uint32_t ITMask) {
  // First count the trailing zeros of the IT mask.
  uint32_t TZ = llvm::countTrailingZeros(ITMask);
  if (TZ > 3) {
#ifdef LLDB_CONFIGURATION_DEBUG
    printf("Encoding error: IT Mask '0000'\n");
#endif
    return 0;
  }
  return (4 - TZ);
}

// Init ITState.  Note that at least one bit is always 1 in mask.
bool ITSession::InitIT(uint32_t bits7_0) {
  ITCounter = CountITSize(Bits32(bits7_0, 3, 0));
  if (ITCounter == 0)
    return false;

  // A8.6.50 IT
  unsigned short FirstCond = Bits32(bits7_0, 7, 4);
  if (FirstCond == 0xF) {
#ifdef LLDB_CONFIGURATION_DEBUG
    printf("Encoding error: IT FirstCond '1111'\n");
#endif
    return false;
  }
  if (FirstCond == 0xE && ITCounter != 1) {
#ifdef LLDB_CONFIGURATION_DEBUG
    printf("Encoding error: IT FirstCond '1110' && Mask != '1000'\n");
#endif
    return false;
  }

  ITState = bits7_0;
  return true;
}

// Update ITState if necessary.
void ITSession::ITAdvance() {
  // assert(ITCounter);
  --ITCounter;
  if (ITCounter == 0)
    ITState = 0;
  else {
    unsigned short NewITState4_0 = Bits32(ITState, 4, 0) << 1;
    SetBits32(ITState, 4, 0, NewITState4_0);
  }
}

// Return true if we're inside an IT Block.
bool ITSession::InITBlock() { return ITCounter != 0; }

// Return true if we're the last instruction inside an IT Block.
bool ITSession::LastInITBlock() { return ITCounter == 1; }

// Get condition bits for the current thumb instruction.
uint32_t ITSession::GetCond() {
  if (InITBlock())
    return Bits32(ITState, 7, 4);
  else
    return COND_AL;
}

// ARM constants used during decoding
#define REG_RD 0
#define LDM_REGLIST 1
#define SP_REG 13
#define LR_REG 14
#define PC_REG 15
#define PC_REGLIST_BIT 0x8000

#define ARMv4 (1u << 0)
#define ARMv4T (1u << 1)
#define ARMv5T (1u << 2)
#define ARMv5TE (1u << 3)
#define ARMv5TEJ (1u << 4)
#define ARMv6 (1u << 5)
#define ARMv6K (1u << 6)
#define ARMv6T2 (1u << 7)
#define ARMv7 (1u << 8)
#define ARMv7S (1u << 9)
#define ARMv8 (1u << 10)
#define ARMvAll (0xffffffffu)

#define ARMV4T_ABOVE                                                           \
  (ARMv4T | ARMv5T | ARMv5TE | ARMv5TEJ | ARMv6 | ARMv6K | ARMv6T2 | ARMv7 |   \
   ARMv7S | ARMv8)
#define ARMV5_ABOVE                                                            \
  (ARMv5T | ARMv5TE | ARMv5TEJ | ARMv6 | ARMv6K | ARMv6T2 | ARMv7 | ARMv7S |   \
   ARMv8)
#define ARMV5TE_ABOVE                                                          \
  (ARMv5TE | ARMv5TEJ | ARMv6 | ARMv6K | ARMv6T2 | ARMv7 | ARMv7S | ARMv8)
#define ARMV5J_ABOVE                                                           \
  (ARMv5TEJ | ARMv6 | ARMv6K | ARMv6T2 | ARMv7 | ARMv7S | ARMv8)
#define ARMV6_ABOVE (ARMv6 | ARMv6K | ARMv6T2 | ARMv7 | ARMv7S | ARMv8)
#define ARMV6T2_ABOVE (ARMv6T2 | ARMv7 | ARMv7S | ARMv8)
#define ARMV7_ABOVE (ARMv7 | ARMv7S | ARMv8)

#define No_VFP 0
#define VFPv1 (1u << 1)
#define VFPv2 (1u << 2)
#define VFPv3 (1u << 3)
#define AdvancedSIMD (1u << 4)

#define VFPv1_ABOVE (VFPv1 | VFPv2 | VFPv3 | AdvancedSIMD)
#define VFPv2_ABOVE (VFPv2 | VFPv3 | AdvancedSIMD)
#define VFPv2v3 (VFPv2 | VFPv3)

//----------------------------------------------------------------------
//
// EmulateInstructionARM implementation
//
//----------------------------------------------------------------------

void EmulateInstructionARM::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void EmulateInstructionARM::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ConstString EmulateInstructionARM::GetPluginNameStatic() {
  static ConstString g_name("arm");
  return g_name;
}

const char *EmulateInstructionARM::GetPluginDescriptionStatic() {
  return "Emulate instructions for the ARM architecture.";
}

EmulateInstruction *
EmulateInstructionARM::CreateInstance(const ArchSpec &arch,
                                      InstructionType inst_type) {
  if (EmulateInstructionARM::SupportsEmulatingInstructionsOfTypeStatic(
          inst_type)) {
    if (arch.GetTriple().getArch() == llvm::Triple::arm) {
      std::unique_ptr<EmulateInstructionARM> emulate_insn_ap(
          new EmulateInstructionARM(arch));

      if (emulate_insn_ap.get())
        return emulate_insn_ap.release();
    } else if (arch.GetTriple().getArch() == llvm::Triple::thumb) {
      std::unique_ptr<EmulateInstructionARM> emulate_insn_ap(
          new EmulateInstructionARM(arch));

      if (emulate_insn_ap.get())
        return emulate_insn_ap.release();
    }
  }

  return NULL;
}

bool EmulateInstructionARM::SetTargetTriple(const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::arm)
    return true;
  else if (arch.GetTriple().getArch() == llvm::Triple::thumb)
    return true;

  return false;
}

// Write "bits (32) UNKNOWN" to memory address "address".  Helper function for
// many ARM instructions.
bool EmulateInstructionARM::WriteBits32UnknownToMemory(addr_t address) {
  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextWriteMemoryRandomBits;
  context.SetNoArgs();

  uint32_t random_data = rand();
  const uint32_t addr_byte_size = GetAddressByteSize();

  return MemAWrite(context, address, random_data, addr_byte_size);
}

// Write "bits (32) UNKNOWN" to register n.  Helper function for many ARM
// instructions.
bool EmulateInstructionARM::WriteBits32Unknown(int n) {
  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextWriteRegisterRandomBits;
  context.SetNoArgs();

  bool success;
  uint32_t data =
      ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);

  if (!success)
    return false;

  if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n, data))
    return false;

  return true;
}

bool EmulateInstructionARM::GetRegisterInfo(lldb::RegisterKind reg_kind,
                                            uint32_t reg_num,
                                            RegisterInfo &reg_info) {
  if (reg_kind == eRegisterKindGeneric) {
    switch (reg_num) {
    case LLDB_REGNUM_GENERIC_PC:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_pc;
      break;
    case LLDB_REGNUM_GENERIC_SP:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_sp;
      break;
    case LLDB_REGNUM_GENERIC_FP:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_r7;
      break;
    case LLDB_REGNUM_GENERIC_RA:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_lr;
      break;
    case LLDB_REGNUM_GENERIC_FLAGS:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_cpsr;
      break;
    default:
      return false;
    }
  }

  if (reg_kind == eRegisterKindDWARF)
    return GetARMDWARFRegisterInfo(reg_num, reg_info);
  return false;
}

uint32_t EmulateInstructionARM::GetFramePointerRegisterNumber() const {
  if (m_arch.GetTriple().isAndroid())
    return LLDB_INVALID_REGNUM; // Don't use frame pointer on android
  bool is_apple = false;
  if (m_arch.GetTriple().getVendor() == llvm::Triple::Apple)
    is_apple = true;
  switch (m_arch.GetTriple().getOS()) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS:
  case llvm::Triple::WatchOS:
  // NEED_BRIDGEOS_TRIPLE case llvm::Triple::BridgeOS:
    is_apple = true;
    break;
  default:
    break;
  }

  /* On Apple iOS et al, the frame pointer register is always r7.
   * Typically on other ARM systems, thumb code uses r7; arm code uses r11.
   */

  uint32_t fp_regnum = 11;

  if (is_apple)
    fp_regnum = 7;

  if (m_opcode_mode == eModeThumb)
    fp_regnum = 7;

  return fp_regnum;
}

uint32_t EmulateInstructionARM::GetFramePointerDWARFRegisterNumber() const {
  bool is_apple = false;
  if (m_arch.GetTriple().getVendor() == llvm::Triple::Apple)
    is_apple = true;
  switch (m_arch.GetTriple().getOS()) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
  case llvm::Triple::IOS:
    is_apple = true;
    break;
  default:
    break;
  }

  /* On Apple iOS et al, the frame pointer register is always r7.
   * Typically on other ARM systems, thumb code uses r7; arm code uses r11.
   */

  uint32_t fp_regnum = dwarf_r11;

  if (is_apple)
    fp_regnum = dwarf_r7;

  if (m_opcode_mode == eModeThumb)
    fp_regnum = dwarf_r7;

  return fp_regnum;
}

// Push Multiple Registers stores multiple registers to the stack, storing to
// consecutive memory locations ending just below the address in SP, and
// updates
// SP to point to the start of the stored data.
bool EmulateInstructionARM::EmulatePUSH(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); 
        NullCheckIfThumbEE(13); 
        address = SP - 4*BitCount(registers);

        for (i = 0 to 14)
        {
            if (registers<i> == '1')
            {
                if i == 13 && i != LowestSetBit(registers) // Only possible for encoding A1 
                    MemA[address,4] = bits(32) UNKNOWN;
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
            }
        }

        if (registers<15> == '1') // Only possible for encoding A1 or A2 
            MemA[address,4] = PCStoreValue();
        
        SP = SP - 4*BitCount(registers);
    }
#endif

  bool success = false;
  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    uint32_t registers = 0;
    uint32_t Rt; // the source register
    switch (encoding) {
    case eEncodingT1:
      registers = Bits32(opcode, 7, 0);
      // The M bit represents LR.
      if (Bit32(opcode, 8))
        registers |= (1u << 14);
      // if BitCount(registers) < 1 then UNPREDICTABLE;
      if (BitCount(registers) < 1)
        return false;
      break;
    case eEncodingT2:
      // Ignore bits 15 & 13.
      registers = Bits32(opcode, 15, 0) & ~0xa000;
      // if BitCount(registers) < 2 then UNPREDICTABLE;
      if (BitCount(registers) < 2)
        return false;
      break;
    case eEncodingT3:
      Rt = Bits32(opcode, 15, 12);
      // if BadReg(t) then UNPREDICTABLE;
      if (BadReg(Rt))
        return false;
      registers = (1u << Rt);
      break;
    case eEncodingA1:
      registers = Bits32(opcode, 15, 0);
      // Instead of return false, let's handle the following case as well,
      // which amounts to pushing one reg onto the full descending stacks.
      // if BitCount(register_list) < 2 then SEE STMDB / STMFD;
      break;
    case eEncodingA2:
      Rt = Bits32(opcode, 15, 12);
      // if t == 13 then UNPREDICTABLE;
      if (Rt == dwarf_sp)
        return false;
      registers = (1u << Rt);
      break;
    default:
      return false;
    }
    addr_t sp_offset = addr_byte_size * BitCount(registers);
    addr_t addr = sp - sp_offset;
    uint32_t i;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextPushRegisterOnStack;
    RegisterInfo reg_info;
    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);
    for (i = 0; i < 15; ++i) {
      if (BitIsSet(registers, i)) {
        GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + i, reg_info);
        context.SetRegisterToRegisterPlusOffset(reg_info, sp_reg, addr - sp);
        uint32_t reg_value = ReadCoreReg(i, &success);
        if (!success)
          return false;
        if (!MemAWrite(context, addr, reg_value, addr_byte_size))
          return false;
        addr += addr_byte_size;
      }
    }

    if (BitIsSet(registers, 15)) {
      GetRegisterInfo(eRegisterKindDWARF, dwarf_pc, reg_info);
      context.SetRegisterToRegisterPlusOffset(reg_info, sp_reg, addr - sp);
      const uint32_t pc = ReadCoreReg(PC_REG, &success);
      if (!success)
        return false;
      if (!MemAWrite(context, addr, pc, addr_byte_size))
        return false;
    }

    context.type = EmulateInstruction::eContextAdjustStackPointer;
    context.SetImmediateSigned(-sp_offset);

    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_SP, sp - sp_offset))
      return false;
  }
  return true;
}

// Pop Multiple Registers loads multiple registers from the stack, loading from
// consecutive memory locations staring at the address in SP, and updates
// SP to point just above the loaded data.
bool EmulateInstructionARM::EmulatePOP(const uint32_t opcode,
                                       const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); NullCheckIfThumbEE(13);
        address = SP;
        for i = 0 to 14
            if registers<i> == '1' then
                R[i] = if UnalignedAllowed then MemU[address,4] else MemA[address,4]; address = address + 4;
        if registers<15> == '1' then
            if UnalignedAllowed then
                LoadWritePC(MemU[address,4]);
            else 
                LoadWritePC(MemA[address,4]);
        if registers<13> == '0' then SP = SP + 4*BitCount(registers);
        if registers<13> == '1' then SP = bits(32) UNKNOWN;
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    uint32_t registers = 0;
    uint32_t Rt; // the destination register
    switch (encoding) {
    case eEncodingT1:
      registers = Bits32(opcode, 7, 0);
      // The P bit represents PC.
      if (Bit32(opcode, 8))
        registers |= (1u << 15);
      // if BitCount(registers) < 1 then UNPREDICTABLE;
      if (BitCount(registers) < 1)
        return false;
      break;
    case eEncodingT2:
      // Ignore bit 13.
      registers = Bits32(opcode, 15, 0) & ~0x2000;
      // if BitCount(registers) < 2 || (P == '1' && M == '1') then
      // UNPREDICTABLE;
      if (BitCount(registers) < 2 || (Bit32(opcode, 15) && Bit32(opcode, 14)))
        return false;
      // if registers<15> == '1' && InITBlock() && !LastInITBlock() then
      // UNPREDICTABLE;
      if (BitIsSet(registers, 15) && InITBlock() && !LastInITBlock())
        return false;
      break;
    case eEncodingT3:
      Rt = Bits32(opcode, 15, 12);
      // if t == 13 || (t == 15 && InITBlock() && !LastInITBlock()) then
      // UNPREDICTABLE;
      if (Rt == 13)
        return false;
      if (Rt == 15 && InITBlock() && !LastInITBlock())
        return false;
      registers = (1u << Rt);
      break;
    case eEncodingA1:
      registers = Bits32(opcode, 15, 0);
      // Instead of return false, let's handle the following case as well,
      // which amounts to popping one reg from the full descending stacks.
      // if BitCount(register_list) < 2 then SEE LDM / LDMIA / LDMFD;

      // if registers<13> == '1' && ArchVersion() >= 7 then UNPREDICTABLE;
      if (BitIsSet(opcode, 13) && ArchVersion() >= ARMv7)
        return false;
      break;
    case eEncodingA2:
      Rt = Bits32(opcode, 15, 12);
      // if t == 13 then UNPREDICTABLE;
      if (Rt == dwarf_sp)
        return false;
      registers = (1u << Rt);
      break;
    default:
      return false;
    }
    addr_t sp_offset = addr_byte_size * BitCount(registers);
    addr_t addr = sp;
    uint32_t i, data;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextPopRegisterOffStack;

    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);

    for (i = 0; i < 15; ++i) {
      if (BitIsSet(registers, i)) {
        context.SetAddress(addr);
        data = MemARead(context, addr, 4, 0, &success);
        if (!success)
          return false;
        if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + i,
                                   data))
          return false;
        addr += addr_byte_size;
      }
    }

    if (BitIsSet(registers, 15)) {
      context.SetRegisterPlusOffset(sp_reg, addr - sp);
      data = MemARead(context, addr, 4, 0, &success);
      if (!success)
        return false;
      // In ARMv5T and above, this is an interworking branch.
      if (!LoadWritePC(context, data))
        return false;
      // addr += addr_byte_size;
    }

    context.type = EmulateInstruction::eContextAdjustStackPointer;
    context.SetImmediateSigned(sp_offset);

    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_SP, sp + sp_offset))
      return false;
  }
  return true;
}

// Set r7 or ip to point to saved value residing within the stack.
// ADD (SP plus immediate)
bool EmulateInstructionARM::EmulateADDRdSPImm(const uint32_t opcode,
                                              const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, imm32, '0');
        if d == 15 then
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    uint32_t Rd; // the destination register
    uint32_t imm32;
    switch (encoding) {
    case eEncodingT1:
      Rd = 7;
      imm32 = Bits32(opcode, 7, 0) << 2; // imm32 = ZeroExtend(imm8:'00', 32)
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
      break;
    default:
      return false;
    }
    addr_t sp_offset = imm32;
    addr_t addr = sp + sp_offset; // a pointer to the stack area

    EmulateInstruction::Context context;
    if (Rd == GetFramePointerRegisterNumber())
      context.type = eContextSetFramePointer;
    else
      context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);
    context.SetRegisterPlusOffset(sp_reg, sp_offset);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + Rd,
                               addr))
      return false;
  }
  return true;
}

// Set r7 or ip to the current stack pointer.
// MOV (register)
bool EmulateInstructionARM::EmulateMOVRdSP(const uint32_t opcode,
                                           const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = R[m];
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                // APSR.C unchanged
                // APSR.V unchanged
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    uint32_t Rd; // the destination register
    switch (encoding) {
    case eEncodingT1:
      Rd = 7;
      break;
    case eEncodingA1:
      Rd = 12;
      break;
    default:
      return false;
    }

    EmulateInstruction::Context context;
    if (Rd == GetFramePointerRegisterNumber())
      context.type = EmulateInstruction::eContextSetFramePointer;
    else
      context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);
    context.SetRegisterPlusOffset(sp_reg, 0);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + Rd, sp))
      return false;
  }
  return true;
}

// Move from high register (r8-r15) to low register (r0-r7).
// MOV (register)
bool EmulateInstructionARM::EmulateMOVLowHigh(const uint32_t opcode,
                                              const ARMEncoding encoding) {
  return EmulateMOVRdRm(opcode, encoding);
}

// Move from register to register.
// MOV (register)
bool EmulateInstructionARM::EmulateMOVRdRm(const uint32_t opcode,
                                           const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = R[m];
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                // APSR.C unchanged
                // APSR.V unchanged
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rm; // the source register
    uint32_t Rd; // the destination register
    bool setflags;
    switch (encoding) {
    case eEncodingT1:
      Rd = Bit32(opcode, 7) << 3 | Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 6, 3);
      setflags = false;
      if (Rd == 15 && InITBlock() && !LastInITBlock())
        return false;
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      setflags = true;
      if (InITBlock())
        return false;
      break;
    case eEncodingT3:
      Rd = Bits32(opcode, 11, 8);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      // if setflags && (BadReg(d) || BadReg(m)) then UNPREDICTABLE;
      if (setflags && (BadReg(Rd) || BadReg(Rm)))
        return false;
      // if !setflags && (d == 15 || m == 15 || (d == 13 && m == 13)) then
      // UNPREDICTABLE;
      if (!setflags && (Rd == 15 || Rm == 15 || (Rd == 13 && Rm == 13)))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }
    uint32_t result = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    // The context specifies that Rm is to be moved into Rd.
    EmulateInstruction::Context context;
    if (Rd == 13)
      context.type = EmulateInstruction::eContextAdjustStackPointer;
    else
      context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rm, dwarf_reg);
    context.SetRegisterPlusOffset(dwarf_reg, 0);

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags))
      return false;
  }
  return true;
}

// Move (immediate) writes an immediate value to the destination register.  It
// can optionally update the condition flags based on the value.
// MOV (immediate)
bool EmulateInstructionARM::EmulateMOVRdImm(const uint32_t opcode,
                                            const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = imm32;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
    }
#endif

  if (ConditionPassed(opcode)) {
    uint32_t Rd;    // the destination register
    uint32_t imm32; // the immediate value to be written to Rd
    uint32_t carry =
        0; // the carry bit after ThumbExpandImm_C or ARMExpandImm_C.
           // for setflags == false, this value is a don't care initialized to
           // 0 to silence the static analyzer
    bool setflags;
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 10, 8);
      setflags = !InITBlock();
      imm32 = Bits32(opcode, 7, 0); // imm32 = ZeroExtend(imm8, 32)
      carry = APSR_C;

      break;

    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm_C(opcode, APSR_C, carry);
      if (BadReg(Rd))
        return false;

      break;

    case eEncodingT3: {
      // d = UInt(Rd); setflags = FALSE; imm32 = ZeroExtend(imm4:i:imm3:imm8,
      // 32);
      Rd = Bits32(opcode, 11, 8);
      setflags = false;
      uint32_t imm4 = Bits32(opcode, 19, 16);
      uint32_t imm3 = Bits32(opcode, 14, 12);
      uint32_t i = Bit32(opcode, 26);
      uint32_t imm8 = Bits32(opcode, 7, 0);
      imm32 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;

      // if BadReg(d) then UNPREDICTABLE;
      if (BadReg(Rd))
        return false;
    } break;

    case eEncodingA1:
      // d = UInt(Rd); setflags = (S == '1'); (imm32, carry) =
      // ARMExpandImm_C(imm12, APSR.C);
      Rd = Bits32(opcode, 15, 12);
      setflags = BitIsSet(opcode, 20);
      imm32 = ARMExpandImm_C(opcode, APSR_C, carry);

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if ((Rd == 15) && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);

      break;

    case eEncodingA2: {
      // d = UInt(Rd); setflags = FALSE; imm32 = ZeroExtend(imm4:imm12, 32);
      Rd = Bits32(opcode, 15, 12);
      setflags = false;
      uint32_t imm4 = Bits32(opcode, 19, 16);
      uint32_t imm12 = Bits32(opcode, 11, 0);
      imm32 = (imm4 << 12) | imm12;

      // if d == 15 then UNPREDICTABLE;
      if (Rd == 15)
        return false;
    } break;

    default:
      return false;
    }
    uint32_t result = imm32;

    // The context specifies that an immediate is to be moved into Rd.
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// MUL multiplies two register values.  The least significant 32 bits of the
// result are written to the destination
// register.  These 32 bits do not depend on whether the source register values
// are considered to be signed values or unsigned values.
//
// Optionally, it can update the condition flags based on the result.  In the
// Thumb instruction set, this option is limited to only a few forms of the
// instruction.
bool EmulateInstructionARM::EmulateMUL(const uint32_t opcode,
                                       const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        operand1 = SInt(R[n]); // operand1 = UInt(R[n]) produces the same final results 
        operand2 = SInt(R[m]); // operand2 = UInt(R[m]) produces the same final results 
        result = operand1 * operand2; 
        R[d] = result<31:0>; 
        if setflags then
            APSR.N = result<31>; 
            APSR.Z = IsZeroBit(result); 
            if ArchVersion() == 4 then
                APSR.C = bit UNKNOWN; 
            // else APSR.C unchanged 
            // APSR.V always unchanged
#endif

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t n;
    uint32_t m;
    bool setflags;

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rdm); n = UInt(Rn); m = UInt(Rdm); setflags = !InITBlock();
      d = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 2, 0);
      setflags = !InITBlock();

      // if ArchVersion() < 6 && d == n then UNPREDICTABLE;
      if ((ArchVersion() < ARMv6) && (d == n))
        return false;

      break;

    case eEncodingT2:
      // d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = FALSE;
      d = Bits32(opcode, 11, 8);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);
      setflags = false;

      // if BadReg(d) || BadReg(n) || BadReg(m) then UNPREDICTABLE;
      if (BadReg(d) || BadReg(n) || BadReg(m))
        return false;

      break;

    case eEncodingA1:
      // d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1');
      d = Bits32(opcode, 19, 16);
      n = Bits32(opcode, 3, 0);
      m = Bits32(opcode, 11, 8);
      setflags = BitIsSet(opcode, 20);

      // if d == 15 || n == 15 || m == 15 then UNPREDICTABLE;
      if ((d == 15) || (n == 15) || (m == 15))
        return false;

      // if ArchVersion() < 6 && d == n then UNPREDICTABLE;
      if ((ArchVersion() < ARMv6) && (d == n))
        return false;

      break;

    default:
      return false;
    }

    bool success = false;

    // operand1 = SInt(R[n]); // operand1 = UInt(R[n]) produces the same final
    // results
    uint64_t operand1 =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    // operand2 = SInt(R[m]); // operand2 = UInt(R[m]) produces the same final
    // results
    uint64_t operand2 =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    // result = operand1 * operand2;
    uint64_t result = operand1 * operand2;

    // R[d] = result<31:0>;
    RegisterInfo op1_reg;
    RegisterInfo op2_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, op1_reg);
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, op2_reg);

    EmulateInstruction::Context context;
    context.type = eContextArithmetic;
    context.SetRegisterRegisterOperands(op1_reg, op2_reg);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + d,
                               (0x0000ffff & result)))
      return false;

    // if setflags then
    if (setflags) {
      // APSR.N = result<31>;
      // APSR.Z = IsZeroBit(result);
      m_new_inst_cpsr = m_opcode_cpsr;
      SetBit32(m_new_inst_cpsr, CPSR_N_POS, Bit32(result, 31));
      SetBit32(m_new_inst_cpsr, CPSR_Z_POS, result == 0 ? 1 : 0);
      if (m_new_inst_cpsr != m_opcode_cpsr) {
        if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                                   LLDB_REGNUM_GENERIC_FLAGS, m_new_inst_cpsr))
          return false;
      }

      // if ArchVersion() == 4 then
      // APSR.C = bit UNKNOWN;
    }
  }
  return true;
}

// Bitwise NOT (immediate) writes the bitwise inverse of an immediate value to
// the destination register. It can optionally update the condition flags based
// on the value.
bool EmulateInstructionARM::EmulateMVNImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        result = NOT(imm32);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
    }
#endif

  if (ConditionPassed(opcode)) {
    uint32_t Rd;    // the destination register
    uint32_t imm32; // the output after ThumbExpandImm_C or ARMExpandImm_C
    uint32_t carry; // the carry bit after ThumbExpandImm_C or ARMExpandImm_C
    bool setflags;
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 11, 8);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm_C(opcode, APSR_C, carry);
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      setflags = BitIsSet(opcode, 20);
      imm32 = ARMExpandImm_C(opcode, APSR_C, carry);

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }
    uint32_t result = ~imm32;

    // The context specifies that an immediate is to be moved into Rd.
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// Bitwise NOT (register) writes the bitwise inverse of a register value to the
// destination register. It can optionally update the condition flags based on
// the result.
bool EmulateInstructionARM::EmulateMVNReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = NOT(shifted);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
    }
#endif

  if (ConditionPassed(opcode)) {
    uint32_t Rm; // the source register
    uint32_t Rd; // the destination register
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    bool setflags;
    uint32_t carry; // the carry bit after the shift operation
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      shift_t = SRType_LSL;
      shift_n = 0;
      if (InITBlock())
        return false;
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);
      // if (BadReg(d) || BadReg(m)) then UNPREDICTABLE;
      if (BadReg(Rd) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftARM(opcode, shift_t);
      break;
    default:
      return false;
    }
    bool success = false;
    uint32_t value = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted =
        Shift_C(value, shift_t, shift_n, APSR_C, carry, &success);
    if (!success)
      return false;
    uint32_t result = ~shifted;

    // The context specifies that an immediate is to be moved into Rd.
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// PC relative immediate load into register, possibly followed by ADD (SP plus
// register).
// LDR (literal)
bool EmulateInstructionARM::EmulateLDRRtPCRelative(const uint32_t opcode,
                                                   const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); NullCheckIfThumbEE(15);
        base = Align(PC,4);
        address = if add then (base + imm32) else (base - imm32);
        data = MemU[address,4];
        if t == 15 then
            if address<1:0> == '00' then LoadWritePC(data); else UNPREDICTABLE;
        elsif UnalignedSupport() || address<1:0> = '00' then
            R[t] = data;
        else // Can only apply before ARMv7
            if CurrentInstrSet() == InstrSet_ARM then
                R[t] = ROR(data, 8*UInt(address<1:0>));
            else
                R[t] = bits(32) UNKNOWN;
    }
#endif

  if (ConditionPassed(opcode)) {
    bool success = false;
    const uint32_t pc = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;

    // PC relative immediate load context
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo pc_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_pc, pc_reg);
    context.SetRegisterPlusOffset(pc_reg, 0);

    uint32_t Rt;    // the destination register
    uint32_t imm32; // immediate offset from the PC
    bool add;       // +imm32 or -imm32?
    addr_t base;    // the base address
    addr_t address; // the PC relative address
    uint32_t data;  // the literal data value from the PC relative load
    switch (encoding) {
    case eEncodingT1:
      Rt = Bits32(opcode, 10, 8);
      imm32 = Bits32(opcode, 7, 0) << 2; // imm32 = ZeroExtend(imm8:'00', 32);
      add = true;
      break;
    case eEncodingT2:
      Rt = Bits32(opcode, 15, 12);
      imm32 = Bits32(opcode, 11, 0) << 2; // imm32 = ZeroExtend(imm12, 32);
      add = BitIsSet(opcode, 23);
      if (Rt == 15 && InITBlock() && !LastInITBlock())
        return false;
      break;
    default:
      return false;
    }

    base = Align(pc, 4);
    if (add)
      address = base + imm32;
    else
      address = base - imm32;

    context.SetRegisterPlusOffset(pc_reg, address - base);
    data = MemURead(context, address, 4, 0, &success);
    if (!success)
      return false;

    if (Rt == 15) {
      if (Bits32(address, 1, 0) == 0) {
        // In ARMv5T and above, this is an interworking branch.
        if (!LoadWritePC(context, data))
          return false;
      } else
        return false;
    } else if (UnalignedSupport() || Bits32(address, 1, 0) == 0) {
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + Rt,
                                 data))
        return false;
    } else // We don't handle ARM for now.
      return false;
  }
  return true;
}

// An add operation to adjust the SP.
// ADD (SP plus immediate)
bool EmulateInstructionARM::EmulateADDSPImm(const uint32_t opcode,
                                            const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, imm32, '0');
        if d == 15 then // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    uint32_t imm32; // the immediate operand
    uint32_t d;
    bool setflags;
    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); setflags = FALSE; imm32 = ZeroExtend(imm8:'00', 32);
      d = Bits32(opcode, 10, 8);
      imm32 = (Bits32(opcode, 7, 0) << 2);
      setflags = false;
      break;

    case eEncodingT2:
      // d = 13; setflags = FALSE; imm32 = ZeroExtend(imm7:'00', 32);
      d = 13;
      imm32 = ThumbImm7Scaled(opcode); // imm32 = ZeroExtend(imm7:'00', 32)
      setflags = false;
      break;

    case eEncodingT3:
      // d = UInt(Rd); setflags = (S == "1"); imm32 =
      // ThumbExpandImm(i:imm3:imm8);
      d = Bits32(opcode, 11, 8);
      imm32 = ThumbExpandImm(opcode);
      setflags = Bit32(opcode, 20);

      // if Rd == "1111" && S == "1" then SEE CMN (immediate);
      if (d == 15 && setflags == 1)
        return false; // CMN (immediate) not yet supported

      // if d == 15 && S == "0" then UNPREDICTABLE;
      if (d == 15 && setflags == 0)
        return false;
      break;

    case eEncodingT4: {
      // if Rn == '1111' then SEE ADR;
      // d = UInt(Rd); setflags = FALSE; imm32 = ZeroExtend(i:imm3:imm8, 32);
      d = Bits32(opcode, 11, 8);
      setflags = false;
      uint32_t i = Bit32(opcode, 26);
      uint32_t imm3 = Bits32(opcode, 14, 12);
      uint32_t imm8 = Bits32(opcode, 7, 0);
      imm32 = (i << 11) | (imm3 << 8) | imm8;

      // if d == 15 then UNPREDICTABLE;
      if (d == 15)
        return false;
    } break;

    default:
      return false;
    }
    // (result, carry, overflow) = AddWithCarry(R[n], imm32, '0');
    AddWithCarryResult res = AddWithCarry(sp, imm32, 0);

    EmulateInstruction::Context context;
    if (d == 13)
      context.type = EmulateInstruction::eContextAdjustStackPointer;
    else
      context.type = EmulateInstruction::eContextRegisterPlusOffset;

    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);
    context.SetRegisterPlusOffset(sp_reg, res.result - sp);

    if (d == 15) {
      if (!ALUWritePC(context, res.result))
        return false;
    } else {
      // R[d] = result;
      // if setflags then
      //     APSR.N = result<31>;
      //     APSR.Z = IsZeroBit(result);
      //     APSR.C = carry;
      //     APSR.V = overflow;
      if (!WriteCoreRegOptionalFlags(context, res.result, d, setflags,
                                     res.carry_out, res.overflow))
        return false;
    }
  }
  return true;
}

// An add operation to adjust the SP.
// ADD (SP plus register)
bool EmulateInstructionARM::EmulateADDSPRm(const uint32_t opcode,
                                           const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(SP, shifted, '0');
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    uint32_t Rm; // the second operand
    switch (encoding) {
    case eEncodingT2:
      Rm = Bits32(opcode, 6, 3);
      break;
    default:
      return false;
    }
    int32_t reg_value = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    addr_t addr = (int32_t)sp + reg_value; // the adjusted stack pointer value

    EmulateInstruction::Context context;
    context.type = eContextArithmetic;
    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);

    RegisterInfo other_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rm, other_reg);
    context.SetRegisterRegisterOperands(sp_reg, other_reg);

    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_SP, addr))
      return false;
  }
  return true;
}

// Branch with Link and Exchange Instruction Sets (immediate) calls a
// subroutine at a PC-relative address, and changes instruction set from ARM to
// Thumb, or from Thumb to ARM.
// BLX (immediate)
bool EmulateInstructionARM::EmulateBLXImmediate(const uint32_t opcode,
                                                const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        if CurrentInstrSet() == InstrSet_ARM then
            LR = PC - 4;
        else
            LR = PC<31:1> : '1';
        if targetInstrSet == InstrSet_ARM then
            targetAddress = Align(PC,4) + imm32;
        else
            targetAddress = PC + imm32;
        SelectInstrSet(targetInstrSet);
        BranchWritePC(targetAddress);
    }
#endif

  bool success = true;

  if (ConditionPassed(opcode)) {
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRelativeBranchImmediate;
    const uint32_t pc = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;
    addr_t lr;     // next instruction address
    addr_t target; // target address
    int32_t imm32; // PC-relative offset
    switch (encoding) {
    case eEncodingT1: {
      lr = pc | 1u; // return address
      uint32_t S = Bit32(opcode, 26);
      uint32_t imm10 = Bits32(opcode, 25, 16);
      uint32_t J1 = Bit32(opcode, 13);
      uint32_t J2 = Bit32(opcode, 11);
      uint32_t imm11 = Bits32(opcode, 10, 0);
      uint32_t I1 = !(J1 ^ S);
      uint32_t I2 = !(J2 ^ S);
      uint32_t imm25 =
          (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
      imm32 = llvm::SignExtend32<25>(imm25);
      target = pc + imm32;
      SelectInstrSet(eModeThumb);
      context.SetISAAndImmediateSigned(eModeThumb, 4 + imm32);
      if (InITBlock() && !LastInITBlock())
        return false;
      break;
    }
    case eEncodingT2: {
      lr = pc | 1u; // return address
      uint32_t S = Bit32(opcode, 26);
      uint32_t imm10H = Bits32(opcode, 25, 16);
      uint32_t J1 = Bit32(opcode, 13);
      uint32_t J2 = Bit32(opcode, 11);
      uint32_t imm10L = Bits32(opcode, 10, 1);
      uint32_t I1 = !(J1 ^ S);
      uint32_t I2 = !(J2 ^ S);
      uint32_t imm25 =
          (S << 24) | (I1 << 23) | (I2 << 22) | (imm10H << 12) | (imm10L << 2);
      imm32 = llvm::SignExtend32<25>(imm25);
      target = Align(pc, 4) + imm32;
      SelectInstrSet(eModeARM);
      context.SetISAAndImmediateSigned(eModeARM, 4 + imm32);
      if (InITBlock() && !LastInITBlock())
        return false;
      break;
    }
    case eEncodingA1:
      lr = pc - 4; // return address
      imm32 = llvm::SignExtend32<26>(Bits32(opcode, 23, 0) << 2);
      target = Align(pc, 4) + imm32;
      SelectInstrSet(eModeARM);
      context.SetISAAndImmediateSigned(eModeARM, 8 + imm32);
      break;
    case eEncodingA2:
      lr = pc - 4; // return address
      imm32 = llvm::SignExtend32<26>(Bits32(opcode, 23, 0) << 2 |
                                     Bits32(opcode, 24, 24) << 1);
      target = pc + imm32;
      SelectInstrSet(eModeThumb);
      context.SetISAAndImmediateSigned(eModeThumb, 8 + imm32);
      break;
    default:
      return false;
    }
    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_RA, lr))
      return false;
    if (!BranchWritePC(context, target))
      return false;
    if (m_opcode_cpsr != m_new_inst_cpsr)
      if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                                 LLDB_REGNUM_GENERIC_FLAGS, m_new_inst_cpsr))
        return false;
  }
  return true;
}

// Branch with Link and Exchange (register) calls a subroutine at an address
// and instruction set specified by a register.
// BLX (register)
bool EmulateInstructionARM::EmulateBLXRm(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        target = R[m];
        if CurrentInstrSet() == InstrSet_ARM then
            next_instr_addr = PC - 4;
            LR = next_instr_addr;
        else
            next_instr_addr = PC - 2;
            LR = next_instr_addr<31:1> : '1';
        BXWritePC(target);
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextAbsoluteBranchRegister;
    const uint32_t pc = ReadCoreReg(PC_REG, &success);
    addr_t lr; // next instruction address
    if (!success)
      return false;
    uint32_t Rm; // the register with the target address
    switch (encoding) {
    case eEncodingT1:
      lr = (pc - 2) | 1u; // return address
      Rm = Bits32(opcode, 6, 3);
      // if m == 15 then UNPREDICTABLE;
      if (Rm == 15)
        return false;
      if (InITBlock() && !LastInITBlock())
        return false;
      break;
    case eEncodingA1:
      lr = pc - 4; // return address
      Rm = Bits32(opcode, 3, 0);
      // if m == 15 then UNPREDICTABLE;
      if (Rm == 15)
        return false;
      break;
    default:
      return false;
    }
    addr_t target = ReadCoreReg(Rm, &success);
    if (!success)
      return false;
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rm, dwarf_reg);
    context.SetRegister(dwarf_reg);
    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_RA, lr))
      return false;
    if (!BXWritePC(context, target))
      return false;
  }
  return true;
}

// Branch and Exchange causes a branch to an address and instruction set
// specified by a register.
bool EmulateInstructionARM::EmulateBXRm(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        BXWritePC(R[m]);
    }
#endif

  if (ConditionPassed(opcode)) {
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextAbsoluteBranchRegister;
    uint32_t Rm; // the register with the target address
    switch (encoding) {
    case eEncodingT1:
      Rm = Bits32(opcode, 6, 3);
      if (InITBlock() && !LastInITBlock())
        return false;
      break;
    case eEncodingA1:
      Rm = Bits32(opcode, 3, 0);
      break;
    default:
      return false;
    }
    bool success = false;
    addr_t target = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rm, dwarf_reg);
    context.SetRegister(dwarf_reg);
    if (!BXWritePC(context, target))
      return false;
  }
  return true;
}

// Branch and Exchange Jazelle attempts to change to Jazelle state. If the
// attempt fails, it branches to an address and instruction set specified by a
// register as though it were a BX instruction.
//
// TODO: Emulate Jazelle architecture?
//       We currently assume that switching to Jazelle state fails, thus
//       treating BXJ as a BX operation.
bool EmulateInstructionARM::EmulateBXJRm(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        if JMCR.JE == '0' || CurrentInstrSet() == InstrSet_ThumbEE then
            BXWritePC(R[m]);
        else
            if JazelleAcceptsExecution() then
                SwitchToJazelleExecution();
            else
                SUBARCHITECTURE_DEFINED handler call;
    }
#endif

  if (ConditionPassed(opcode)) {
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextAbsoluteBranchRegister;
    uint32_t Rm; // the register with the target address
    switch (encoding) {
    case eEncodingT1:
      Rm = Bits32(opcode, 19, 16);
      if (BadReg(Rm))
        return false;
      if (InITBlock() && !LastInITBlock())
        return false;
      break;
    case eEncodingA1:
      Rm = Bits32(opcode, 3, 0);
      if (Rm == 15)
        return false;
      break;
    default:
      return false;
    }
    bool success = false;
    addr_t target = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rm, dwarf_reg);
    context.SetRegister(dwarf_reg);
    if (!BXWritePC(context, target))
      return false;
  }
  return true;
}

// Set r7 to point to some ip offset.
// SUB (immediate)
bool EmulateInstructionARM::EmulateSUBR7IPImm(const uint32_t opcode,
                                              const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), '1');
        if d == 15 then // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

  if (ConditionPassed(opcode)) {
    bool success = false;
    const addr_t ip = ReadCoreReg(12, &success);
    if (!success)
      return false;
    uint32_t imm32;
    switch (encoding) {
    case eEncodingA1:
      imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
      break;
    default:
      return false;
    }
    addr_t ip_offset = imm32;
    addr_t addr = ip - ip_offset; // the adjusted ip value

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r12, dwarf_reg);
    context.SetRegisterPlusOffset(dwarf_reg, -ip_offset);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r7, addr))
      return false;
  }
  return true;
}

// Set ip to point to some stack offset.
// SUB (SP minus immediate)
bool EmulateInstructionARM::EmulateSUBIPSPImm(const uint32_t opcode,
                                              const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), '1');
        if d == 15 then // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

  if (ConditionPassed(opcode)) {
    bool success = false;
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    uint32_t imm32;
    switch (encoding) {
    case eEncodingA1:
      imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
      break;
    default:
      return false;
    }
    addr_t sp_offset = imm32;
    addr_t addr = sp - sp_offset; // the adjusted stack pointer value

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP, dwarf_reg);
    context.SetRegisterPlusOffset(dwarf_reg, -sp_offset);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r12, addr))
      return false;
  }
  return true;
}

// This instruction subtracts an immediate value from the SP value, and writes
// the result to the destination register.
//
// If Rd == 13 => A sub operation to adjust the SP -- allocate space for local
// storage.
bool EmulateInstructionARM::EmulateSUBSPImm(const uint32_t opcode,
                                            const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(SP, NOT(imm32), '1');
        if d == 15 then        // Can only occur for ARM encoding
           ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
    }
#endif

  bool success = false;
  if (ConditionPassed(opcode)) {
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;

    uint32_t Rd;
    bool setflags;
    uint32_t imm32;
    switch (encoding) {
    case eEncodingT1:
      Rd = 13;
      setflags = false;
      imm32 = ThumbImm7Scaled(opcode); // imm32 = ZeroExtend(imm7:'00', 32)
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
      if (Rd == 15 && setflags)
        return EmulateCMPImm(opcode, eEncodingT2);
      if (Rd == 15 && !setflags)
        return false;
      break;
    case eEncodingT3:
      Rd = Bits32(opcode, 11, 8);
      setflags = false;
      imm32 = ThumbImm12(opcode); // imm32 = ZeroExtend(i:imm3:imm8, 32)
      if (Rd == 15)
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      setflags = BitIsSet(opcode, 20);
      imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }
    AddWithCarryResult res = AddWithCarry(sp, ~imm32, 1);

    EmulateInstruction::Context context;
    if (Rd == 13) {
      uint64_t imm64 = imm32; // Need to expand it to 64 bits before attempting
                              // to negate it, or the wrong
      // value gets passed down to context.SetImmediateSigned.
      context.type = EmulateInstruction::eContextAdjustStackPointer;
      context.SetImmediateSigned(-imm64); // the stack pointer offset
    } else {
      context.type = EmulateInstruction::eContextImmediate;
      context.SetNoArgs();
    }

    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// A store operation to the stack that also updates the SP.
bool EmulateInstructionARM::EmulateSTRRtSP(const uint32_t opcode,
                                           const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        MemU[address,4] = if t == 15 then PCStoreValue() else R[t];
        if wback then R[n] = offset_addr;
    }
#endif

  bool success = false;
  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    uint32_t Rt; // the source register
    uint32_t imm12;
    uint32_t
        Rn; // This function assumes Rn is the SP, but we should verify that.

    bool index;
    bool add;
    bool wback;
    switch (encoding) {
    case eEncodingA1:
      Rt = Bits32(opcode, 15, 12);
      imm12 = Bits32(opcode, 11, 0);
      Rn = Bits32(opcode, 19, 16);

      if (Rn != 13) // 13 is the SP reg on ARM.  Verify that Rn == SP.
        return false;

      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = (BitIsClear(opcode, 24) || BitIsSet(opcode, 21));

      if (wback && ((Rn == 15) || (Rn == Rt)))
        return false;
      break;
    default:
      return false;
    }
    addr_t offset_addr;
    if (add)
      offset_addr = sp + imm12;
    else
      offset_addr = sp - imm12;

    addr_t addr;
    if (index)
      addr = offset_addr;
    else
      addr = sp;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextPushRegisterOnStack;
    RegisterInfo sp_reg;
    RegisterInfo dwarf_reg;

    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rt, dwarf_reg);
    context.SetRegisterToRegisterPlusOffset(dwarf_reg, sp_reg, addr - sp);
    if (Rt != 15) {
      uint32_t reg_value = ReadCoreReg(Rt, &success);
      if (!success)
        return false;
      if (!MemUWrite(context, addr, reg_value, addr_byte_size))
        return false;
    } else {
      const uint32_t pc = ReadCoreReg(PC_REG, &success);
      if (!success)
        return false;
      if (!MemUWrite(context, addr, pc, addr_byte_size))
        return false;
    }

    if (wback) {
      context.type = EmulateInstruction::eContextAdjustStackPointer;
      context.SetImmediateSigned(addr - sp);
      if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                                 LLDB_REGNUM_GENERIC_SP, offset_addr))
        return false;
    }
  }
  return true;
}

// Vector Push stores multiple extension registers to the stack. It also
// updates SP to point to the start of the stored data.
bool EmulateInstructionARM::EmulateVPUSH(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(13);
        address = SP - imm32;
        SP = SP - imm32;
        if single_regs then
            for r = 0 to regs-1
                MemA[address,4] = S[d+r]; address = address+4;
        else
            for r = 0 to regs-1
                // Store as two word-aligned words in the correct order for
                // current endianness.
                MemA[address,4] = if BigEndian() then D[d+r]<63:32> else D[d+r]<31:0>;
                MemA[address+4,4] = if BigEndian() then D[d+r]<31:0> else D[d+r]<63:32>;
                address = address+8;
    }
#endif

  bool success = false;
  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    bool single_regs;
    uint32_t d;     // UInt(D:Vd) or UInt(Vd:D) starting register
    uint32_t imm32; // stack offset
    uint32_t regs;  // number of registers
    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1:
      single_regs = false;
      d = Bit32(opcode, 22) << 4 | Bits32(opcode, 15, 12);
      imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
      // If UInt(imm8) is odd, see "FSTMX".
      regs = Bits32(opcode, 7, 0) / 2;
      // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
      if (regs == 0 || regs > 16 || (d + regs) > 32)
        return false;
      break;
    case eEncodingT2:
    case eEncodingA2:
      single_regs = true;
      d = Bits32(opcode, 15, 12) << 1 | Bit32(opcode, 22);
      imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
      regs = Bits32(opcode, 7, 0);
      // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
      if (regs == 0 || regs > 16 || (d + regs) > 32)
        return false;
      break;
    default:
      return false;
    }
    uint32_t start_reg = single_regs ? dwarf_s0 : dwarf_d0;
    uint32_t reg_byte_size = single_regs ? addr_byte_size : addr_byte_size * 2;
    addr_t sp_offset = imm32;
    addr_t addr = sp - sp_offset;
    uint32_t i;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextPushRegisterOnStack;

    RegisterInfo dwarf_reg;
    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);
    for (i = 0; i < regs; ++i) {
      GetRegisterInfo(eRegisterKindDWARF, start_reg + d + i, dwarf_reg);
      context.SetRegisterToRegisterPlusOffset(dwarf_reg, sp_reg, addr - sp);
      // uint64_t to accommodate 64-bit registers.
      uint64_t reg_value = ReadRegisterUnsigned(&dwarf_reg, 0, &success);
      if (!success)
        return false;
      if (!MemAWrite(context, addr, reg_value, reg_byte_size))
        return false;
      addr += reg_byte_size;
    }

    context.type = EmulateInstruction::eContextAdjustStackPointer;
    context.SetImmediateSigned(-sp_offset);

    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_SP, sp - sp_offset))
      return false;
  }
  return true;
}

// Vector Pop loads multiple extension registers from the stack. It also
// updates SP to point just above the loaded data.
bool EmulateInstructionARM::EmulateVPOP(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(13);
        address = SP;
        SP = SP + imm32;
        if single_regs then
            for r = 0 to regs-1
                S[d+r] = MemA[address,4]; address = address+4;
        else
            for r = 0 to regs-1
                word1 = MemA[address,4]; word2 = MemA[address+4,4]; address = address+8;
                // Combine the word-aligned words in the correct order for
                // current endianness.
                D[d+r] = if BigEndian() then word1:word2 else word2:word1;
    }
#endif

  bool success = false;
  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();
    const addr_t sp = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;
    bool single_regs;
    uint32_t d;     // UInt(D:Vd) or UInt(Vd:D) starting register
    uint32_t imm32; // stack offset
    uint32_t regs;  // number of registers
    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1:
      single_regs = false;
      d = Bit32(opcode, 22) << 4 | Bits32(opcode, 15, 12);
      imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
      // If UInt(imm8) is odd, see "FLDMX".
      regs = Bits32(opcode, 7, 0) / 2;
      // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
      if (regs == 0 || regs > 16 || (d + regs) > 32)
        return false;
      break;
    case eEncodingT2:
    case eEncodingA2:
      single_regs = true;
      d = Bits32(opcode, 15, 12) << 1 | Bit32(opcode, 22);
      imm32 = Bits32(opcode, 7, 0) * addr_byte_size;
      regs = Bits32(opcode, 7, 0);
      // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
      if (regs == 0 || regs > 16 || (d + regs) > 32)
        return false;
      break;
    default:
      return false;
    }
    uint32_t start_reg = single_regs ? dwarf_s0 : dwarf_d0;
    uint32_t reg_byte_size = single_regs ? addr_byte_size : addr_byte_size * 2;
    addr_t sp_offset = imm32;
    addr_t addr = sp;
    uint32_t i;
    uint64_t data; // uint64_t to accommodate 64-bit registers.

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextPopRegisterOffStack;

    RegisterInfo dwarf_reg;
    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);
    for (i = 0; i < regs; ++i) {
      GetRegisterInfo(eRegisterKindDWARF, start_reg + d + i, dwarf_reg);
      context.SetAddress(addr);
      data = MemARead(context, addr, reg_byte_size, 0, &success);
      if (!success)
        return false;
      if (!WriteRegisterUnsigned(context, &dwarf_reg, data))
        return false;
      addr += reg_byte_size;
    }

    context.type = EmulateInstruction::eContextAdjustStackPointer;
    context.SetImmediateSigned(sp_offset);

    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_SP, sp + sp_offset))
      return false;
  }
  return true;
}

// SVC (previously SWI)
bool EmulateInstructionARM::EmulateSVC(const uint32_t opcode,
                                       const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        CallSupervisor();
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const uint32_t pc = ReadCoreReg(PC_REG, &success);
    addr_t lr; // next instruction address
    if (!success)
      return false;
    uint32_t imm32; // the immediate constant
    uint32_t mode;  // ARM or Thumb mode
    switch (encoding) {
    case eEncodingT1:
      lr = (pc + 2) | 1u; // return address
      imm32 = Bits32(opcode, 7, 0);
      mode = eModeThumb;
      break;
    case eEncodingA1:
      lr = pc + 4; // return address
      imm32 = Bits32(opcode, 23, 0);
      mode = eModeARM;
      break;
    default:
      return false;
    }

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextSupervisorCall;
    context.SetISAAndImmediate(mode, imm32);
    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_RA, lr))
      return false;
  }
  return true;
}

// If Then makes up to four following instructions (the IT block) conditional.
bool EmulateInstructionARM::EmulateIT(const uint32_t opcode,
                                      const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    EncodingSpecificOperations();
    ITSTATE.IT<7:0> = firstcond:mask;
#endif

  m_it_session.InitIT(Bits32(opcode, 7, 0));
  return true;
}

bool EmulateInstructionARM::EmulateNop(const uint32_t opcode,
                                       const ARMEncoding encoding) {
  // NOP, nothing to do...
  return true;
}

// Branch causes a branch to a target address.
bool EmulateInstructionARM::EmulateB(const uint32_t opcode,
                                     const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations();
        BranchWritePC(PC + imm32);
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRelativeBranchImmediate;
    const uint32_t pc = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;
    addr_t target; // target address
    int32_t imm32; // PC-relative offset
    switch (encoding) {
    case eEncodingT1:
      // The 'cond' field is handled in EmulateInstructionARM::CurrentCond().
      imm32 = llvm::SignExtend32<9>(Bits32(opcode, 7, 0) << 1);
      target = pc + imm32;
      context.SetISAAndImmediateSigned(eModeThumb, 4 + imm32);
      break;
    case eEncodingT2:
      imm32 = llvm::SignExtend32<12>(Bits32(opcode, 10, 0) << 1);
      target = pc + imm32;
      context.SetISAAndImmediateSigned(eModeThumb, 4 + imm32);
      break;
    case eEncodingT3:
      // The 'cond' field is handled in EmulateInstructionARM::CurrentCond().
      {
        if (Bits32(opcode, 25, 23) == 7)
          return false; // See Branches and miscellaneous control on page
                        // A6-235.

        uint32_t S = Bit32(opcode, 26);
        uint32_t imm6 = Bits32(opcode, 21, 16);
        uint32_t J1 = Bit32(opcode, 13);
        uint32_t J2 = Bit32(opcode, 11);
        uint32_t imm11 = Bits32(opcode, 10, 0);
        uint32_t imm21 =
            (S << 20) | (J2 << 19) | (J1 << 18) | (imm6 << 12) | (imm11 << 1);
        imm32 = llvm::SignExtend32<21>(imm21);
        target = pc + imm32;
        context.SetISAAndImmediateSigned(eModeThumb, 4 + imm32);
        break;
      }
    case eEncodingT4: {
      uint32_t S = Bit32(opcode, 26);
      uint32_t imm10 = Bits32(opcode, 25, 16);
      uint32_t J1 = Bit32(opcode, 13);
      uint32_t J2 = Bit32(opcode, 11);
      uint32_t imm11 = Bits32(opcode, 10, 0);
      uint32_t I1 = !(J1 ^ S);
      uint32_t I2 = !(J2 ^ S);
      uint32_t imm25 =
          (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1);
      imm32 = llvm::SignExtend32<25>(imm25);
      target = pc + imm32;
      context.SetISAAndImmediateSigned(eModeThumb, 4 + imm32);
      break;
    }
    case eEncodingA1:
      imm32 = llvm::SignExtend32<26>(Bits32(opcode, 23, 0) << 2);
      target = pc + imm32;
      context.SetISAAndImmediateSigned(eModeARM, 8 + imm32);
      break;
    default:
      return false;
    }
    if (!BranchWritePC(context, target))
      return false;
  }
  return true;
}

// Compare and Branch on Nonzero and Compare and Branch on Zero compare the
// value in a register with zero and conditionally branch forward a constant
// value.  They do not affect the condition flags. CBNZ, CBZ
bool EmulateInstructionARM::EmulateCB(const uint32_t opcode,
                                      const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    EncodingSpecificOperations();
    if nonzero ^ IsZero(R[n]) then
        BranchWritePC(PC + imm32);
#endif

  bool success = false;

  // Read the register value from the operand register Rn.
  uint32_t reg_val = ReadCoreReg(Bits32(opcode, 2, 0), &success);
  if (!success)
    return false;

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextRelativeBranchImmediate;
  const uint32_t pc = ReadCoreReg(PC_REG, &success);
  if (!success)
    return false;

  addr_t target;  // target address
  uint32_t imm32; // PC-relative offset to branch forward
  bool nonzero;
  switch (encoding) {
  case eEncodingT1:
    imm32 = Bit32(opcode, 9) << 6 | Bits32(opcode, 7, 3) << 1;
    nonzero = BitIsSet(opcode, 11);
    target = pc + imm32;
    context.SetISAAndImmediateSigned(eModeThumb, 4 + imm32);
    break;
  default:
    return false;
  }
  if (m_ignore_conditions || (nonzero ^ (reg_val == 0)))
    if (!BranchWritePC(context, target))
      return false;

  return true;
}

// Table Branch Byte causes a PC-relative forward branch using a table of
// single byte offsets.
// A base register provides a pointer to the table, and a second register
// supplies an index into the table.
// The branch length is twice the value of the byte returned from the table.
//
// Table Branch Halfword causes a PC-relative forward branch using a table of
// single halfword offsets.
// A base register provides a pointer to the table, and a second register
// supplies an index into the table.
// The branch length is twice the value of the halfword returned from the
// table. TBB, TBH
bool EmulateInstructionARM::EmulateTB(const uint32_t opcode,
                                      const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    if is_tbh then
        halfwords = UInt(MemU[R[n]+LSL(R[m],1), 2]);
    else
        halfwords = UInt(MemU[R[n]+R[m], 1]);
    BranchWritePC(PC + 2*halfwords);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rn; // the base register which contains the address of the table of
                 // branch lengths
    uint32_t Rm; // the index register which contains an integer pointing to a
                 // byte/halfword in the table
    bool is_tbh; // true if table branch halfword
    switch (encoding) {
    case eEncodingT1:
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      is_tbh = BitIsSet(opcode, 4);
      if (Rn == 13 || BadReg(Rm))
        return false;
      if (InITBlock() && !LastInITBlock())
        return false;
      break;
    default:
      return false;
    }

    // Read the address of the table from the operand register Rn. The PC can
    // be used, in which case the table immediately follows this instruction.
    uint32_t base = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // the table index
    uint32_t index = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    // the offsetted table address
    addr_t addr = base + (is_tbh ? index * 2 : index);

    // PC-relative offset to branch forward
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextTableBranchReadMemory;
    uint32_t offset = MemURead(context, addr, is_tbh ? 2 : 1, 0, &success) * 2;
    if (!success)
      return false;

    const uint32_t pc = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;

    // target address
    addr_t target = pc + offset;
    context.type = EmulateInstruction::eContextRelativeBranchImmediate;
    context.SetISAAndImmediateSigned(eModeThumb, 4 + offset);

    if (!BranchWritePC(context, target))
      return false;
  }

  return true;
}

// This instruction adds an immediate value to a register value, and writes the
// result to the destination register. It can optionally update the condition
// flags based on the result.
bool EmulateInstructionARM::EmulateADDImmThumb(const uint32_t opcode,
                                               const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        (result, carry, overflow) = AddWithCarry(R[n], imm32, '0'); 
        R[d] = result; 
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t n;
    bool setflags;
    uint32_t imm32;
    uint32_t carry_out;

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); n = UInt(Rn); setflags = !InITBlock(); imm32 =
      // ZeroExtend(imm3, 32);
      d = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      imm32 = Bits32(opcode, 8, 6);

      break;

    case eEncodingT2:
      // d = UInt(Rdn); n = UInt(Rdn); setflags = !InITBlock(); imm32 =
      // ZeroExtend(imm8, 32);
      d = Bits32(opcode, 10, 8);
      n = Bits32(opcode, 10, 8);
      setflags = !InITBlock();
      imm32 = Bits32(opcode, 7, 0);

      break;

    case eEncodingT3:
      // if Rd == '1111' && S == '1' then SEE CMN (immediate);
      // d = UInt(Rd); n = UInt(Rn); setflags = (S == '1'); imm32 =
      // ThumbExpandImm(i:imm3:imm8);
      d = Bits32(opcode, 11, 8);
      n = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm_C(opcode, APSR_C, carry_out);

      // if Rn == '1101' then SEE ADD (SP plus immediate);
      if (n == 13)
        return EmulateADDSPImm(opcode, eEncodingT3);

      // if BadReg(d) || n == 15 then UNPREDICTABLE;
      if (BadReg(d) || (n == 15))
        return false;

      break;

    case eEncodingT4: {
      // if Rn == '1111' then SEE ADR;
      // d = UInt(Rd); n = UInt(Rn); setflags = FALSE; imm32 =
      // ZeroExtend(i:imm3:imm8, 32);
      d = Bits32(opcode, 11, 8);
      n = Bits32(opcode, 19, 16);
      setflags = false;
      uint32_t i = Bit32(opcode, 26);
      uint32_t imm3 = Bits32(opcode, 14, 12);
      uint32_t imm8 = Bits32(opcode, 7, 0);
      imm32 = (i << 11) | (imm3 << 8) | imm8;

      // if Rn == '1101' then SEE ADD (SP plus immediate);
      if (n == 13)
        return EmulateADDSPImm(opcode, eEncodingT4);

      // if BadReg(d) then UNPREDICTABLE;
      if (BadReg(d))
        return false;

      break;
    }

    default:
      return false;
    }

    uint64_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    //(result, carry, overflow) = AddWithCarry(R[n], imm32, '0');
    AddWithCarryResult res = AddWithCarry(Rn, imm32, 0);

    RegisterInfo reg_n;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, reg_n);

    EmulateInstruction::Context context;
    context.type = eContextArithmetic;
    context.SetRegisterPlusOffset(reg_n, imm32);

    // R[d] = result;
    // if setflags then
    // APSR.N = result<31>;
    // APSR.Z = IsZeroBit(result);
    // APSR.C = carry;
    // APSR.V = overflow;
    if (!WriteCoreRegOptionalFlags(context, res.result, d, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// This instruction adds an immediate value to a register value, and writes the
// result to the destination register.  It can optionally update the condition
// flags based on the result.
bool EmulateInstructionARM::EmulateADDImmARM(const uint32_t opcode,
                                             const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], imm32, '0');
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn;
    uint32_t
        imm32; // the immediate value to be added to the value obtained from Rn
    bool setflags;
    switch (encoding) {
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    AddWithCarryResult res = AddWithCarry(val1, imm32, 0);

    EmulateInstruction::Context context;
    if (Rd == 13)
      context.type = EmulateInstruction::eContextAdjustStackPointer;
    else if (Rd == GetFramePointerRegisterNumber())
      context.type = EmulateInstruction::eContextSetFramePointer;
    else
      context.type = EmulateInstruction::eContextRegisterPlusOffset;

    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, Rn, dwarf_reg);
    context.SetRegisterPlusOffset(dwarf_reg, imm32);

    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// This instruction adds a register value and an optionally-shifted register
// value, and writes the result to the destination register. It can optionally
// update the condition flags based on the result.
bool EmulateInstructionARM::EmulateADDReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], shifted, '0');
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn, Rm;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    bool setflags;
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 2, 0);
      Rn = Bits32(opcode, 5, 3);
      Rm = Bits32(opcode, 8, 6);
      setflags = !InITBlock();
      shift_t = SRType_LSL;
      shift_n = 0;
      break;
    case eEncodingT2:
      Rd = Rn = Bit32(opcode, 7) << 3 | Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 6, 3);
      setflags = false;
      shift_t = SRType_LSL;
      shift_n = 0;
      if (Rn == 15 && Rm == 15)
        return false;
      if (Rd == 15 && InITBlock() && !LastInITBlock())
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftARM(opcode, shift_t);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // Read the second operand.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;
    AddWithCarryResult res = AddWithCarry(val1, shifted, 0);

    EmulateInstruction::Context context;
    context.type = eContextArithmetic;
    RegisterInfo op1_reg;
    RegisterInfo op2_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rn, op1_reg);
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rm, op2_reg);
    context.SetRegisterRegisterOperands(op1_reg, op2_reg);

    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// Compare Negative (immediate) adds a register value and an immediate value.
// It updates the condition flags based on the result, and discards the result.
bool EmulateInstructionARM::EmulateCMNImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], imm32, '0');
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rn;    // the first operand
  uint32_t imm32; // the immediate value to be compared with
  switch (encoding) {
  case eEncodingT1:
    Rn = Bits32(opcode, 19, 16);
    imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
    if (Rn == 15)
      return false;
    break;
  case eEncodingA1:
    Rn = Bits32(opcode, 19, 16);
    imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
    break;
  default:
    return false;
  }
  // Read the register value from the operand register Rn.
  uint32_t reg_val = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  AddWithCarryResult res = AddWithCarry(reg_val, imm32, 0);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();
  return WriteFlags(context, res.result, res.carry_out, res.overflow);
}

// Compare Negative (register) adds a register value and an optionally-shifted
// register value. It updates the condition flags based on the result, and
// discards the result.
bool EmulateInstructionARM::EmulateCMNReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], shifted, '0');
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rn; // the first operand
  uint32_t Rm; // the second operand
  ARM_ShifterType shift_t;
  uint32_t shift_n; // the shift applied to the value read from Rm
  switch (encoding) {
  case eEncodingT1:
    Rn = Bits32(opcode, 2, 0);
    Rm = Bits32(opcode, 5, 3);
    shift_t = SRType_LSL;
    shift_n = 0;
    break;
  case eEncodingT2:
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    shift_n = DecodeImmShiftThumb(opcode, shift_t);
    // if n == 15 || BadReg(m) then UNPREDICTABLE;
    if (Rn == 15 || BadReg(Rm))
      return false;
    break;
  case eEncodingA1:
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    shift_n = DecodeImmShiftARM(opcode, shift_t);
    break;
  default:
    return false;
  }
  // Read the register value from register Rn.
  uint32_t val1 = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  // Read the register value from register Rm.
  uint32_t val2 = ReadCoreReg(Rm, &success);
  if (!success)
    return false;

  uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C, &success);
  if (!success)
    return false;
  AddWithCarryResult res = AddWithCarry(val1, shifted, 0);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();
  return WriteFlags(context, res.result, res.carry_out, res.overflow);
}

// Compare (immediate) subtracts an immediate value from a register value. It
// updates the condition flags based on the result, and discards the result.
bool EmulateInstructionARM::EmulateCMPImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], NOT(imm32), '1');
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rn;    // the first operand
  uint32_t imm32; // the immediate value to be compared with
  switch (encoding) {
  case eEncodingT1:
    Rn = Bits32(opcode, 10, 8);
    imm32 = Bits32(opcode, 7, 0);
    break;
  case eEncodingT2:
    Rn = Bits32(opcode, 19, 16);
    imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
    if (Rn == 15)
      return false;
    break;
  case eEncodingA1:
    Rn = Bits32(opcode, 19, 16);
    imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)
    break;
  default:
    return false;
  }
  // Read the register value from the operand register Rn.
  uint32_t reg_val = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  AddWithCarryResult res = AddWithCarry(reg_val, ~imm32, 1);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();
  return WriteFlags(context, res.result, res.carry_out, res.overflow);
}

// Compare (register) subtracts an optionally-shifted register value from a
// register value. It updates the condition flags based on the result, and
// discards the result.
bool EmulateInstructionARM::EmulateCMPReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], NOT(shifted), '1');
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rn; // the first operand
  uint32_t Rm; // the second operand
  ARM_ShifterType shift_t;
  uint32_t shift_n; // the shift applied to the value read from Rm
  switch (encoding) {
  case eEncodingT1:
    Rn = Bits32(opcode, 2, 0);
    Rm = Bits32(opcode, 5, 3);
    shift_t = SRType_LSL;
    shift_n = 0;
    break;
  case eEncodingT2:
    Rn = Bit32(opcode, 7) << 3 | Bits32(opcode, 2, 0);
    Rm = Bits32(opcode, 6, 3);
    shift_t = SRType_LSL;
    shift_n = 0;
    if (Rn < 8 && Rm < 8)
      return false;
    if (Rn == 15 || Rm == 15)
      return false;
    break;
  case eEncodingT3:
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    shift_n = DecodeImmShiftThumb(opcode, shift_t);
    if (Rn == 15 || BadReg(Rm))
      return false;
    break;
  case eEncodingA1:
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    shift_n = DecodeImmShiftARM(opcode, shift_t);
    break;
  default:
    return false;
  }
  // Read the register value from register Rn.
  uint32_t val1 = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  // Read the register value from register Rm.
  uint32_t val2 = ReadCoreReg(Rm, &success);
  if (!success)
    return false;

  uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C, &success);
  if (!success)
    return false;
  AddWithCarryResult res = AddWithCarry(val1, ~shifted, 1);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();
  return WriteFlags(context, res.result, res.carry_out, res.overflow);
}

// Arithmetic Shift Right (immediate) shifts a register value right by an
// immediate number of bits, shifting in copies of its sign bit, and writes the
// result to the destination register.  It can optionally update the condition
// flags based on the result.
bool EmulateInstructionARM::EmulateASRImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_ASR, shift_n, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  return EmulateShiftImm(opcode, encoding, SRType_ASR);
}

// Arithmetic Shift Right (register) shifts a register value right by a
// variable number of bits, shifting in copies of its sign bit, and writes the
// result to the destination register. The variable number of bits is read from
// the bottom byte of a register. It can optionally update the condition flags
// based on the result.
bool EmulateInstructionARM::EmulateASRReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[m]<7:0>);
        (result, carry) = Shift_C(R[m], SRType_ASR, shift_n, APSR.C);
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            // APSR.V unchanged
#endif

  return EmulateShiftReg(opcode, encoding, SRType_ASR);
}

// Logical Shift Left (immediate) shifts a register value left by an immediate
// number of bits, shifting in zeros, and writes the result to the destination
// register.  It can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateLSLImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_LSL, shift_n, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  return EmulateShiftImm(opcode, encoding, SRType_LSL);
}

// Logical Shift Left (register) shifts a register value left by a variable
// number of bits, shifting in zeros, and writes the result to the destination
// register.  The variable number of bits is read from the bottom byte of a
// register. It can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateLSLReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[m]<7:0>);
        (result, carry) = Shift_C(R[m], SRType_LSL, shift_n, APSR.C);
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            // APSR.V unchanged
#endif

  return EmulateShiftReg(opcode, encoding, SRType_LSL);
}

// Logical Shift Right (immediate) shifts a register value right by an
// immediate number of bits, shifting in zeros, and writes the result to the
// destination register.  It can optionally update the condition flags based on
// the result.
bool EmulateInstructionARM::EmulateLSRImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_LSR, shift_n, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  return EmulateShiftImm(opcode, encoding, SRType_LSR);
}

// Logical Shift Right (register) shifts a register value right by a variable
// number of bits, shifting in zeros, and writes the result to the destination
// register.  The variable number of bits is read from the bottom byte of a
// register. It can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateLSRReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[m]<7:0>);
        (result, carry) = Shift_C(R[m], SRType_LSR, shift_n, APSR.C);
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            // APSR.V unchanged
#endif

  return EmulateShiftReg(opcode, encoding, SRType_LSR);
}

// Rotate Right (immediate) provides the value of the contents of a register
// rotated by a constant value. The bits that are rotated off the right end are
// inserted into the vacated bit positions on the left. It can optionally
// update the condition flags based on the result.
bool EmulateInstructionARM::EmulateRORImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_ROR, shift_n, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  return EmulateShiftImm(opcode, encoding, SRType_ROR);
}

// Rotate Right (register) provides the value of the contents of a register
// rotated by a variable number of bits. The bits that are rotated off the
// right end are inserted into the vacated bit positions on the left. The
// variable number of bits is read from the bottom byte of a register. It can
// optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateRORReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[m]<7:0>);
        (result, carry) = Shift_C(R[m], SRType_ROR, shift_n, APSR.C);
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            // APSR.V unchanged
#endif

  return EmulateShiftReg(opcode, encoding, SRType_ROR);
}

// Rotate Right with Extend provides the value of the contents of a register
// shifted right by one place, with the carry flag shifted into bit [31].
//
// RRX can optionally update the condition flags based on the result.
// In that case, bit [0] is shifted into the carry flag.
bool EmulateInstructionARM::EmulateRRX(const uint32_t opcode,
                                       const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry) = Shift_C(R[m], SRType_RRX, 1, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  return EmulateShiftImm(opcode, encoding, SRType_RRX);
}

bool EmulateInstructionARM::EmulateShiftImm(const uint32_t opcode,
                                            const ARMEncoding encoding,
                                            ARM_ShifterType shift_type) {
  //    assert(shift_type == SRType_ASR
  //           || shift_type == SRType_LSL
  //           || shift_type == SRType_LSR
  //           || shift_type == SRType_ROR
  //           || shift_type == SRType_RRX);

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd;    // the destination register
    uint32_t Rm;    // the first operand register
    uint32_t imm5;  // encoding for the shift amount
    uint32_t carry; // the carry bit after the shift operation
    bool setflags;

    // Special case handling!
    // A8.6.139 ROR (immediate) -- Encoding T1
    ARMEncoding use_encoding = encoding;
    if (shift_type == SRType_ROR && use_encoding == eEncodingT1) {
      // Morph the T1 encoding from the ARM Architecture Manual into T2
      // encoding to have the same decoding of bit fields as the other Thumb2
      // shift operations.
      use_encoding = eEncodingT2;
    }

    switch (use_encoding) {
    case eEncodingT1:
      // Due to the above special case handling!
      if (shift_type == SRType_ROR)
        return false;

      Rd = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      imm5 = Bits32(opcode, 10, 6);
      break;
    case eEncodingT2:
      // A8.6.141 RRX
      // There's no imm form of RRX instructions.
      if (shift_type == SRType_RRX)
        return false;

      Rd = Bits32(opcode, 11, 8);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      imm5 = Bits32(opcode, 14, 12) << 2 | Bits32(opcode, 7, 6);
      if (BadReg(Rd) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      imm5 = Bits32(opcode, 11, 7);
      break;
    default:
      return false;
    }

    // A8.6.139 ROR (immediate)
    if (shift_type == SRType_ROR && imm5 == 0)
      shift_type = SRType_RRX;

    // Get the first operand.
    uint32_t value = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    // Decode the shift amount if not RRX.
    uint32_t amt =
        (shift_type == SRType_RRX ? 1 : DecodeImmShift(shift_type, imm5));

    uint32_t result = Shift_C(value, shift_type, amt, APSR_C, carry, &success);
    if (!success)
      return false;

    // The context specifies that an immediate is to be moved into Rd.
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

bool EmulateInstructionARM::EmulateShiftReg(const uint32_t opcode,
                                            const ARMEncoding encoding,
                                            ARM_ShifterType shift_type) {
  // assert(shift_type == SRType_ASR
  //        || shift_type == SRType_LSL
  //        || shift_type == SRType_LSR
  //        || shift_type == SRType_ROR);

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd; // the destination register
    uint32_t Rn; // the first operand register
    uint32_t
        Rm; // the register whose bottom byte contains the amount to shift by
    uint32_t carry; // the carry bit after the shift operation
    bool setflags;
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 2, 0);
      Rn = Rd;
      Rm = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      if (BadReg(Rd) || BadReg(Rn) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 3, 0);
      Rm = Bits32(opcode, 11, 8);
      setflags = BitIsSet(opcode, 20);
      if (Rd == 15 || Rn == 15 || Rm == 15)
        return false;
      break;
    default:
      return false;
    }

    // Get the first operand.
    uint32_t value = ReadCoreReg(Rn, &success);
    if (!success)
      return false;
    // Get the Rm register content.
    uint32_t val = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    // Get the shift amount.
    uint32_t amt = Bits32(val, 7, 0);

    uint32_t result = Shift_C(value, shift_type, amt, APSR_C, carry, &success);
    if (!success)
      return false;

    // The context specifies that an immediate is to be moved into Rd.
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// LDM loads multiple registers from consecutive memory locations, using an
// address from a base register.  Optionally the address just above the highest
// of those locations can be written back to the base register.
bool EmulateInstructionARM::EmulateLDM(const uint32_t opcode,
                                       const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed()
        EncodingSpecificOperations(); NullCheckIfThumbEE (n);
        address = R[n];
                  
        for i = 0 to 14
            if registers<i> == '1' then
                R[i] = MemA[address, 4]; address = address + 4;
        if registers<15> == '1' then
            LoadWritePC (MemA[address, 4]);
                  
        if wback && registers<n> == '0' then R[n] = R[n] + 4 * BitCount (registers);
        if wback && registers<n> == '1' then R[n] = bits(32) UNKNOWN; // Only possible for encoding A1

#endif

  bool success = false;
  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t registers = 0;
    bool wback;
    const uint32_t addr_byte_size = GetAddressByteSize();
    switch (encoding) {
    case eEncodingT1:
      // n = UInt(Rn); registers = '00000000':register_list; wback =
      // (registers<n> == '0');
      n = Bits32(opcode, 10, 8);
      registers = Bits32(opcode, 7, 0);
      registers = registers & 0x00ff; // Make sure the top 8 bits are zeros.
      wback = BitIsClear(registers, n);
      // if BitCount(registers) < 1 then UNPREDICTABLE;
      if (BitCount(registers) < 1)
        return false;
      break;
    case eEncodingT2:
      // if W == '1' && Rn == '1101' then SEE POP;
      // n = UInt(Rn); registers = P:M:'0':register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      registers = registers & 0xdfff; // Make sure bit 13 is zero.
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 2 || (P == '1' && M == '1') then
      // UNPREDICTABLE;
      if ((n == 15) || (BitCount(registers) < 2) ||
          (BitIsSet(opcode, 14) && BitIsSet(opcode, 15)))
        return false;

      // if registers<15> == '1' && InITBlock() && !LastInITBlock() then
      // UNPREDICTABLE;
      if (BitIsSet(registers, 15) && InITBlock() && !LastInITBlock())
        return false;

      // if wback && registers<n> == '1' then UNPREDICTABLE;
      if (wback && BitIsSet(registers, n))
        return false;
      break;

    case eEncodingA1:
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      wback = BitIsSet(opcode, 21);
      if ((n == 15) || (BitCount(registers) < 1))
        return false;
      break;
    default:
      return false;
    }

    int32_t offset = 0;
    const addr_t base_address =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, dwarf_reg);
    context.SetRegisterPlusOffset(dwarf_reg, offset);

    for (int i = 0; i < 14; ++i) {
      if (BitIsSet(registers, i)) {
        context.type = EmulateInstruction::eContextRegisterPlusOffset;
        context.SetRegisterPlusOffset(dwarf_reg, offset);
        if (wback && (n == 13)) // Pop Instruction
        {
          context.type = EmulateInstruction::eContextPopRegisterOffStack;
          context.SetAddress(base_address + offset);
        }

        // R[i] = MemA [address, 4]; address = address + 4;
        uint32_t data = MemARead(context, base_address + offset, addr_byte_size,
                                 0, &success);
        if (!success)
          return false;

        if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + i,
                                   data))
          return false;

        offset += addr_byte_size;
      }
    }

    if (BitIsSet(registers, 15)) {
      // LoadWritePC (MemA [address, 4]);
      context.type = EmulateInstruction::eContextRegisterPlusOffset;
      context.SetRegisterPlusOffset(dwarf_reg, offset);
      uint32_t data =
          MemARead(context, base_address + offset, addr_byte_size, 0, &success);
      if (!success)
        return false;
      // In ARMv5T and above, this is an interworking branch.
      if (!LoadWritePC(context, data))
        return false;
    }

    if (wback && BitIsClear(registers, n)) {
      // R[n] = R[n] + 4 * BitCount (registers)
      int32_t offset = addr_byte_size * BitCount(registers);
      context.type = EmulateInstruction::eContextAdjustBaseRegister;
      context.SetRegisterPlusOffset(dwarf_reg, offset);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 base_address + offset))
        return false;
    }
    if (wback && BitIsSet(registers, n))
      // R[n] bits(32) UNKNOWN;
      return WriteBits32Unknown(n);
  }
  return true;
}

// LDMDA loads multiple registers from consecutive memory locations using an
// address from a base register.
// The consecutive memory locations end at this address and the address just
// below the lowest of those locations can optionally be written back to the
// base register.
bool EmulateInstructionARM::EmulateLDMDA(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        address = R[n] - 4*BitCount(registers) + 4;
                  
        for i = 0 to 14 
            if registers<i> == '1' then
                  R[i] = MemA[address,4]; address = address + 4; 
                  
        if registers<15> == '1' then
            LoadWritePC(MemA[address,4]);
                  
        if wback && registers<n> == '0' then R[n] = R[n] - 4*BitCount(registers); 
        if wback && registers<n> == '1' then R[n] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t registers = 0;
    bool wback;
    const uint32_t addr_byte_size = GetAddressByteSize();

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingA1:
      // n = UInt(Rn); registers = register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
      if ((n == 15) || (BitCount(registers) < 1))
        return false;

      break;

    default:
      return false;
    }
    // address = R[n] - 4*BitCount(registers) + 4;

    int32_t offset = 0;
    addr_t Rn = ReadCoreReg(n, &success);

    if (!success)
      return false;

    addr_t address =
        Rn - (addr_byte_size * BitCount(registers)) + addr_byte_size;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, dwarf_reg);
    context.SetRegisterPlusOffset(dwarf_reg, offset);

    // for i = 0 to 14
    for (int i = 0; i < 14; ++i) {
      // if registers<i> == '1' then
      if (BitIsSet(registers, i)) {
        // R[i] = MemA[address,4]; address = address + 4;
        context.SetRegisterPlusOffset(dwarf_reg, Rn - (address + offset));
        uint32_t data =
            MemARead(context, address + offset, addr_byte_size, 0, &success);
        if (!success)
          return false;
        if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + i,
                                   data))
          return false;
        offset += addr_byte_size;
      }
    }

    // if registers<15> == '1' then
    //     LoadWritePC(MemA[address,4]);
    if (BitIsSet(registers, 15)) {
      context.SetRegisterPlusOffset(dwarf_reg, offset);
      uint32_t data =
          MemARead(context, address + offset, addr_byte_size, 0, &success);
      if (!success)
        return false;
      // In ARMv5T and above, this is an interworking branch.
      if (!LoadWritePC(context, data))
        return false;
    }

    // if wback && registers<n> == '0' then R[n] = R[n] - 4*BitCount(registers);
    if (wback && BitIsClear(registers, n)) {
      if (!success)
        return false;

      offset = (addr_byte_size * BitCount(registers)) * -1;
      context.type = EmulateInstruction::eContextAdjustBaseRegister;
      context.SetImmediateSigned(offset);
      addr_t addr = Rn + offset;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 addr))
        return false;
    }

    // if wback && registers<n> == '1' then R[n] = bits(32) UNKNOWN;
    if (wback && BitIsSet(registers, n))
      return WriteBits32Unknown(n);
  }
  return true;
}

// LDMDB loads multiple registers from consecutive memory locations using an
// address from a base register.  The
// consecutive memory locations end just below this address, and the address of
// the lowest of those locations can be optionally written back to the base
// register.
bool EmulateInstructionARM::EmulateLDMDB(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        address = R[n] - 4*BitCount(registers);
                  
        for i = 0 to 14 
            if registers<i> == '1' then
                  R[i] = MemA[address,4]; address = address + 4; 
        if registers<15> == '1' then
                  LoadWritePC(MemA[address,4]);
                  
        if wback && registers<n> == '0' then R[n] = R[n] - 4*BitCount(registers); 
        if wback && registers<n> == '1' then R[n] = bits(32) UNKNOWN; // Only possible for encoding A1
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t registers = 0;
    bool wback;
    const uint32_t addr_byte_size = GetAddressByteSize();
    switch (encoding) {
    case eEncodingT1:
      // n = UInt(Rn); registers = P:M:'0':register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      registers = registers & 0xdfff; // Make sure bit 13 is a zero.
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 2 || (P == '1' && M == '1') then
      // UNPREDICTABLE;
      if ((n == 15) || (BitCount(registers) < 2) ||
          (BitIsSet(opcode, 14) && BitIsSet(opcode, 15)))
        return false;

      // if registers<15> == '1' && InITBlock() && !LastInITBlock() then
      // UNPREDICTABLE;
      if (BitIsSet(registers, 15) && InITBlock() && !LastInITBlock())
        return false;

      // if wback && registers<n> == '1' then UNPREDICTABLE;
      if (wback && BitIsSet(registers, n))
        return false;

      break;

    case eEncodingA1:
      // n = UInt(Rn); registers = register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
      if ((n == 15) || (BitCount(registers) < 1))
        return false;

      break;

    default:
      return false;
    }

    // address = R[n] - 4*BitCount(registers);

    int32_t offset = 0;
    addr_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);

    if (!success)
      return false;

    addr_t address = Rn - (addr_byte_size * BitCount(registers));
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, dwarf_reg);
    context.SetRegisterPlusOffset(dwarf_reg, Rn - address);

    for (int i = 0; i < 14; ++i) {
      if (BitIsSet(registers, i)) {
        // R[i] = MemA[address,4]; address = address + 4;
        context.SetRegisterPlusOffset(dwarf_reg, Rn - (address + offset));
        uint32_t data =
            MemARead(context, address + offset, addr_byte_size, 0, &success);
        if (!success)
          return false;

        if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + i,
                                   data))
          return false;

        offset += addr_byte_size;
      }
    }

    // if registers<15> == '1' then
    //     LoadWritePC(MemA[address,4]);
    if (BitIsSet(registers, 15)) {
      context.SetRegisterPlusOffset(dwarf_reg, offset);
      uint32_t data =
          MemARead(context, address + offset, addr_byte_size, 0, &success);
      if (!success)
        return false;
      // In ARMv5T and above, this is an interworking branch.
      if (!LoadWritePC(context, data))
        return false;
    }

    // if wback && registers<n> == '0' then R[n] = R[n] - 4*BitCount(registers);
    if (wback && BitIsClear(registers, n)) {
      if (!success)
        return false;

      offset = (addr_byte_size * BitCount(registers)) * -1;
      context.type = EmulateInstruction::eContextAdjustBaseRegister;
      context.SetImmediateSigned(offset);
      addr_t addr = Rn + offset;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 addr))
        return false;
    }

    // if wback && registers<n> == '1' then R[n] = bits(32) UNKNOWN; // Only
    // possible for encoding A1
    if (wback && BitIsSet(registers, n))
      return WriteBits32Unknown(n);
  }
  return true;
}

// LDMIB loads multiple registers from consecutive memory locations using an
// address from a base register.  The
// consecutive memory locations start just above this address, and thea ddress
// of the last of those locations can optinoally be written back to the base
// register.
bool EmulateInstructionARM::EmulateLDMIB(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); 
        address = R[n] + 4;
                  
        for i = 0 to 14 
            if registers<i> == '1' then
                  R[i] = MemA[address,4]; address = address + 4; 
        if registers<15> == '1' then
            LoadWritePC(MemA[address,4]);
                  
        if wback && registers<n> == '0' then R[n] = R[n] + 4*BitCount(registers); 
        if wback && registers<n> == '1' then R[n] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t registers = 0;
    bool wback;
    const uint32_t addr_byte_size = GetAddressByteSize();
    switch (encoding) {
    case eEncodingA1:
      // n = UInt(Rn); registers = register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
      if ((n == 15) || (BitCount(registers) < 1))
        return false;

      break;
    default:
      return false;
    }
    // address = R[n] + 4;

    int32_t offset = 0;
    addr_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);

    if (!success)
      return false;

    addr_t address = Rn + addr_byte_size;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterPlusOffset;
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, dwarf_reg);
    context.SetRegisterPlusOffset(dwarf_reg, offset);

    for (int i = 0; i < 14; ++i) {
      if (BitIsSet(registers, i)) {
        // R[i] = MemA[address,4]; address = address + 4;

        context.SetRegisterPlusOffset(dwarf_reg, offset + addr_byte_size);
        uint32_t data =
            MemARead(context, address + offset, addr_byte_size, 0, &success);
        if (!success)
          return false;

        if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + i,
                                   data))
          return false;

        offset += addr_byte_size;
      }
    }

    // if registers<15> == '1' then
    //     LoadWritePC(MemA[address,4]);
    if (BitIsSet(registers, 15)) {
      context.SetRegisterPlusOffset(dwarf_reg, offset);
      uint32_t data =
          MemARead(context, address + offset, addr_byte_size, 0, &success);
      if (!success)
        return false;
      // In ARMv5T and above, this is an interworking branch.
      if (!LoadWritePC(context, data))
        return false;
    }

    // if wback && registers<n> == '0' then R[n] = R[n] + 4*BitCount(registers);
    if (wback && BitIsClear(registers, n)) {
      if (!success)
        return false;

      offset = addr_byte_size * BitCount(registers);
      context.type = EmulateInstruction::eContextAdjustBaseRegister;
      context.SetImmediateSigned(offset);
      addr_t addr = Rn + offset;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 addr))
        return false;
    }

    // if wback && registers<n> == '1' then R[n] = bits(32) UNKNOWN; // Only
    // possible for encoding A1
    if (wback && BitIsSet(registers, n))
      return WriteBits32Unknown(n);
  }
  return true;
}

// Load Register (immediate) calculates an address from a base register value
// and an immediate offset, loads a word from memory, and writes to a register.
// LDR (immediate, Thumb)
bool EmulateInstructionARM::EmulateLDRRtRnImm(const uint32_t opcode,
                                              const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if (ConditionPassed())
    {
        EncodingSpecificOperations(); NullCheckIfThumbEE(15);
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        data = MemU[address,4];
        if wback then R[n] = offset_addr;
        if t == 15 then
            if address<1:0> == '00' then LoadWritePC(data); else UNPREDICTABLE;
        elsif UnalignedSupport() || address<1:0> = '00' then
            R[t] = data;
        else R[t] = bits(32) UNKNOWN; // Can only apply before ARMv7
    }
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rt;        // the destination register
    uint32_t Rn;        // the base register
    uint32_t imm32;     // the immediate offset used to form the address
    addr_t offset_addr; // the offset address
    addr_t address;     // the calculated address
    uint32_t data;      // the literal data value from memory load
    bool add, index, wback;
    switch (encoding) {
    case eEncodingT1:
      Rt = Bits32(opcode, 2, 0);
      Rn = Bits32(opcode, 5, 3);
      imm32 = Bits32(opcode, 10, 6) << 2; // imm32 = ZeroExtend(imm5:'00', 32);
      // index = TRUE; add = TRUE; wback = FALSE
      add = true;
      index = true;
      wback = false;

      break;

    case eEncodingT2:
      // t = UInt(Rt); n = 13; imm32 = ZeroExtend(imm8:'00', 32);
      Rt = Bits32(opcode, 10, 8);
      Rn = 13;
      imm32 = Bits32(opcode, 7, 0) << 2;

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      break;

    case eEncodingT3:
      // if Rn == '1111' then SEE LDR (literal);
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      Rt = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // if t == 15 && InITBlock() && !LastInITBlock() then UNPREDICTABLE;
      if ((Rt == 15) && InITBlock() && !LastInITBlock())
        return false;

      break;

    case eEncodingT4:
      // if Rn == '1111' then SEE LDR (literal);
      // if P == '1' && U == '1' && W == '0' then SEE LDRT;
      // if Rn == '1101' && P == '0' && U == '1' && W == '1' && imm8 ==
      // '00000100' then SEE POP;
      // if P == '0' && W == '0' then UNDEFINED;
      if (BitIsClear(opcode, 10) && BitIsClear(opcode, 8))
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
      Rt = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0);

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 10);
      add = BitIsSet(opcode, 9);
      wback = BitIsSet(opcode, 8);

      // if (wback && n == t) || (t == 15 && InITBlock() && !LastInITBlock())
      // then UNPREDICTABLE;
      if ((wback && (Rn == Rt)) ||
          ((Rt == 15) && InITBlock() && !LastInITBlock()))
        return false;

      break;

    default:
      return false;
    }
    uint32_t base = ReadCoreReg(Rn, &success);
    if (!success)
      return false;
    if (add)
      offset_addr = base + imm32;
    else
      offset_addr = base - imm32;

    address = (index ? offset_addr : base);

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + Rn, base_reg);
    if (wback) {
      EmulateInstruction::Context ctx;
      if (Rn == 13) {
        ctx.type = eContextAdjustStackPointer;
        ctx.SetImmediateSigned((int32_t)(offset_addr - base));
      } else if (Rn == GetFramePointerRegisterNumber()) {
        ctx.type = eContextSetFramePointer;
        ctx.SetRegisterPlusOffset(base_reg, (int32_t)(offset_addr - base));
      } else {
        ctx.type = EmulateInstruction::eContextAdjustBaseRegister;
        ctx.SetRegisterPlusOffset(base_reg, (int32_t)(offset_addr - base));
      }

      if (!WriteRegisterUnsigned(ctx, eRegisterKindDWARF, dwarf_r0 + Rn,
                                 offset_addr))
        return false;
    }

    // Prepare to write to the Rt register.
    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, (int32_t)(offset_addr - base));

    // Read memory from the address.
    data = MemURead(context, address, 4, 0, &success);
    if (!success)
      return false;

    if (Rt == 15) {
      if (Bits32(address, 1, 0) == 0) {
        if (!LoadWritePC(context, data))
          return false;
      } else
        return false;
    } else if (UnalignedSupport() || Bits32(address, 1, 0) == 0) {
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + Rt,
                                 data))
        return false;
    } else
      WriteBits32Unknown(Rt);
  }
  return true;
}

// STM (Store Multiple Increment After) stores multiple registers to consecutive
// memory locations using an address
// from a base register.  The consecutive memory locations start at this
// address, and the address just above the last of those locations can
// optionally be written back to the base register.
bool EmulateInstructionARM::EmulateSTM(const uint32_t opcode,
                                       const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        address = R[n];
                  
        for i = 0 to 14 
            if registers<i> == '1' then
                if i == n && wback && i != LowestSetBit(registers) then 
                    MemA[address,4] = bits(32) UNKNOWN; // Only possible for encodings T1 and A1
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
                  
        if registers<15> == '1' then // Only possible for encoding A1 
            MemA[address,4] = PCStoreValue();
        if wback then R[n] = R[n] + 4*BitCount(registers);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t registers = 0;
    bool wback;
    const uint32_t addr_byte_size = GetAddressByteSize();

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // n = UInt(Rn); registers = '00000000':register_list; wback = TRUE;
      n = Bits32(opcode, 10, 8);
      registers = Bits32(opcode, 7, 0);
      registers = registers & 0x00ff; // Make sure the top 8 bits are zeros.
      wback = true;

      // if BitCount(registers) < 1 then UNPREDICTABLE;
      if (BitCount(registers) < 1)
        return false;

      break;

    case eEncodingT2:
      // n = UInt(Rn); registers = '0':M:'0':register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      registers = registers & 0x5fff; // Make sure bits 15 & 13 are zeros.
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 2 then UNPREDICTABLE;
      if ((n == 15) || (BitCount(registers) < 2))
        return false;

      // if wback && registers<n> == '1' then UNPREDICTABLE;
      if (wback && BitIsSet(registers, n))
        return false;

      break;

    case eEncodingA1:
      // n = UInt(Rn); registers = register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
      if ((n == 15) || (BitCount(registers) < 1))
        return false;

      break;

    default:
      return false;
    }

    // address = R[n];
    int32_t offset = 0;
    const addr_t address =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterStore;
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    // for i = 0 to 14
    uint32_t lowest_set_bit = 14;
    for (uint32_t i = 0; i < 14; ++i) {
      // if registers<i> == '1' then
      if (BitIsSet(registers, i)) {
        if (i < lowest_set_bit)
          lowest_set_bit = i;
        // if i == n && wback && i != LowestSetBit(registers) then
        if ((i == n) && wback && (i != lowest_set_bit))
          // MemA[address,4] = bits(32) UNKNOWN; // Only possible for encodings
          // T1 and A1
          WriteBits32UnknownToMemory(address + offset);
        else {
          // MemA[address,4] = R[i];
          uint32_t data = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + i,
                                               0, &success);
          if (!success)
            return false;

          RegisterInfo data_reg;
          GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + i, data_reg);
          context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, offset);
          if (!MemAWrite(context, address + offset, data, addr_byte_size))
            return false;
        }

        // address = address + 4;
        offset += addr_byte_size;
      }
    }

    // if registers<15> == '1' then // Only possible for encoding A1
    //     MemA[address,4] = PCStoreValue();
    if (BitIsSet(registers, 15)) {
      RegisterInfo pc_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_pc, pc_reg);
      context.SetRegisterPlusOffset(pc_reg, 8);
      const uint32_t pc = ReadCoreReg(PC_REG, &success);
      if (!success)
        return false;

      if (!MemAWrite(context, address + offset, pc, addr_byte_size))
        return false;
    }

    // if wback then R[n] = R[n] + 4*BitCount(registers);
    if (wback) {
      offset = addr_byte_size * BitCount(registers);
      context.type = EmulateInstruction::eContextAdjustBaseRegister;
      context.SetImmediateSigned(offset);
      addr_t data = address + offset;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 data))
        return false;
    }
  }
  return true;
}

// STMDA (Store Multiple Decrement After) stores multiple registers to
// consecutive memory locations using an address from a base register.  The
// consecutive memory locations end at this address, and the address just below
// the lowest of those locations can optionally be written back to the base
// register.
bool EmulateInstructionARM::EmulateSTMDA(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations();                   
        address = R[n] - 4*BitCount(registers) + 4;
                  
        for i = 0 to 14 
            if registers<i> == '1' then
                if i == n && wback && i != LowestSetBit(registers) then 
                    MemA[address,4] = bits(32) UNKNOWN;
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
                  
        if registers<15> == '1' then 
            MemA[address,4] = PCStoreValue();
                  
        if wback then R[n] = R[n] - 4*BitCount(registers);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t registers = 0;
    bool wback;
    const uint32_t addr_byte_size = GetAddressByteSize();

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingA1:
      // n = UInt(Rn); registers = register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
      if ((n == 15) || (BitCount(registers) < 1))
        return false;
      break;
    default:
      return false;
    }

    // address = R[n] - 4*BitCount(registers) + 4;
    int32_t offset = 0;
    addr_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    addr_t address = Rn - (addr_byte_size * BitCount(registers)) + 4;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterStore;
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    // for i = 0 to 14
    uint32_t lowest_bit_set = 14;
    for (uint32_t i = 0; i < 14; ++i) {
      // if registers<i> == '1' then
      if (BitIsSet(registers, i)) {
        if (i < lowest_bit_set)
          lowest_bit_set = i;
        // if i == n && wback && i != LowestSetBit(registers) then
        if ((i == n) && wback && (i != lowest_bit_set))
          // MemA[address,4] = bits(32) UNKNOWN;
          WriteBits32UnknownToMemory(address + offset);
        else {
          // MemA[address,4] = R[i];
          uint32_t data = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + i,
                                               0, &success);
          if (!success)
            return false;

          RegisterInfo data_reg;
          GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + i, data_reg);
          context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                  Rn - (address + offset));
          if (!MemAWrite(context, address + offset, data, addr_byte_size))
            return false;
        }

        // address = address + 4;
        offset += addr_byte_size;
      }
    }

    // if registers<15> == '1' then
    //    MemA[address,4] = PCStoreValue();
    if (BitIsSet(registers, 15)) {
      RegisterInfo pc_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_pc, pc_reg);
      context.SetRegisterPlusOffset(pc_reg, 8);
      const uint32_t pc = ReadCoreReg(PC_REG, &success);
      if (!success)
        return false;

      if (!MemAWrite(context, address + offset, pc, addr_byte_size))
        return false;
    }

    // if wback then R[n] = R[n] - 4*BitCount(registers);
    if (wback) {
      offset = (addr_byte_size * BitCount(registers)) * -1;
      context.type = EmulateInstruction::eContextAdjustBaseRegister;
      context.SetImmediateSigned(offset);
      addr_t data = Rn + offset;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 data))
        return false;
    }
  }
  return true;
}

// STMDB (Store Multiple Decrement Before) stores multiple registers to
// consecutive memory locations using an address from a base register.  The
// consecutive memory locations end just below this address, and the address of
// the first of those locations can optionally be written back to the base
// register.
bool EmulateInstructionARM::EmulateSTMDB(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        address = R[n] - 4*BitCount(registers);
                  
        for i = 0 to 14 
            if registers<i> == '1' then
                if i == n && wback && i != LowestSetBit(registers) then 
                    MemA[address,4] = bits(32) UNKNOWN; // Only possible for encoding A1
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
                  
        if registers<15> == '1' then // Only possible for encoding A1 
            MemA[address,4] = PCStoreValue();
                  
        if wback then R[n] = R[n] - 4*BitCount(registers);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t registers = 0;
    bool wback;
    const uint32_t addr_byte_size = GetAddressByteSize();

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // if W == '1' && Rn == '1101' then SEE PUSH;
      if ((BitIsSet(opcode, 21)) && (Bits32(opcode, 19, 16) == 13)) {
        // See PUSH
      }
      // n = UInt(Rn); registers = '0':M:'0':register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      registers = registers & 0x5fff; // Make sure bits 15 & 13 are zeros.
      wback = BitIsSet(opcode, 21);
      // if n == 15 || BitCount(registers) < 2 then UNPREDICTABLE;
      if ((n == 15) || BitCount(registers) < 2)
        return false;
      // if wback && registers<n> == '1' then UNPREDICTABLE;
      if (wback && BitIsSet(registers, n))
        return false;
      break;

    case eEncodingA1:
      // if W == '1' && Rn == '1101' && BitCount(register_list) >= 2 then SEE
      // PUSH;
      if (BitIsSet(opcode, 21) && (Bits32(opcode, 19, 16) == 13) &&
          BitCount(Bits32(opcode, 15, 0)) >= 2) {
        // See Push
      }
      // n = UInt(Rn); registers = register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      wback = BitIsSet(opcode, 21);
      // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
      if ((n == 15) || BitCount(registers) < 1)
        return false;
      break;

    default:
      return false;
    }

    // address = R[n] - 4*BitCount(registers);

    int32_t offset = 0;
    addr_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    addr_t address = Rn - (addr_byte_size * BitCount(registers));

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterStore;
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    // for i = 0 to 14
    uint32_t lowest_set_bit = 14;
    for (uint32_t i = 0; i < 14; ++i) {
      // if registers<i> == '1' then
      if (BitIsSet(registers, i)) {
        if (i < lowest_set_bit)
          lowest_set_bit = i;
        // if i == n && wback && i != LowestSetBit(registers) then
        if ((i == n) && wback && (i != lowest_set_bit))
          // MemA[address,4] = bits(32) UNKNOWN; // Only possible for encoding
          // A1
          WriteBits32UnknownToMemory(address + offset);
        else {
          // MemA[address,4] = R[i];
          uint32_t data = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + i,
                                               0, &success);
          if (!success)
            return false;

          RegisterInfo data_reg;
          GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + i, data_reg);
          context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                  Rn - (address + offset));
          if (!MemAWrite(context, address + offset, data, addr_byte_size))
            return false;
        }

        // address = address + 4;
        offset += addr_byte_size;
      }
    }

    // if registers<15> == '1' then // Only possible for encoding A1
    //     MemA[address,4] = PCStoreValue();
    if (BitIsSet(registers, 15)) {
      RegisterInfo pc_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_pc, pc_reg);
      context.SetRegisterPlusOffset(pc_reg, 8);
      const uint32_t pc = ReadCoreReg(PC_REG, &success);
      if (!success)
        return false;

      if (!MemAWrite(context, address + offset, pc, addr_byte_size))
        return false;
    }

    // if wback then R[n] = R[n] - 4*BitCount(registers);
    if (wback) {
      offset = (addr_byte_size * BitCount(registers)) * -1;
      context.type = EmulateInstruction::eContextAdjustBaseRegister;
      context.SetImmediateSigned(offset);
      addr_t data = Rn + offset;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 data))
        return false;
    }
  }
  return true;
}

// STMIB (Store Multiple Increment Before) stores multiple registers to
// consecutive memory locations using an address from a base register.  The
// consecutive memory locations start just above this address, and the address
// of the last of those locations can optionally be written back to the base
// register.
bool EmulateInstructionARM::EmulateSTMIB(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        address = R[n] + 4;
                  
        for i = 0 to 14 
            if registers<i> == '1' then
                if i == n && wback && i != LowestSetBit(registers) then
                    MemA[address,4] = bits(32) UNKNOWN;
                else 
                    MemA[address,4] = R[i];
                address = address + 4;
                  
        if registers<15> == '1' then 
            MemA[address,4] = PCStoreValue();
                  
        if wback then R[n] = R[n] + 4*BitCount(registers);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t registers = 0;
    bool wback;
    const uint32_t addr_byte_size = GetAddressByteSize();

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingA1:
      // n = UInt(Rn); registers = register_list; wback = (W == '1');
      n = Bits32(opcode, 19, 16);
      registers = Bits32(opcode, 15, 0);
      wback = BitIsSet(opcode, 21);

      // if n == 15 || BitCount(registers) < 1 then UNPREDICTABLE;
      if ((n == 15) && (BitCount(registers) < 1))
        return false;
      break;
    default:
      return false;
    }
    // address = R[n] + 4;

    int32_t offset = 0;
    addr_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    addr_t address = Rn + addr_byte_size;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextRegisterStore;
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t lowest_set_bit = 14;
    // for i = 0 to 14
    for (uint32_t i = 0; i < 14; ++i) {
      // if registers<i> == '1' then
      if (BitIsSet(registers, i)) {
        if (i < lowest_set_bit)
          lowest_set_bit = i;
        // if i == n && wback && i != LowestSetBit(registers) then
        if ((i == n) && wback && (i != lowest_set_bit))
          // MemA[address,4] = bits(32) UNKNOWN;
          WriteBits32UnknownToMemory(address + offset);
        // else
        else {
          // MemA[address,4] = R[i];
          uint32_t data = ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + i,
                                               0, &success);
          if (!success)
            return false;

          RegisterInfo data_reg;
          GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + i, data_reg);
          context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                  offset + addr_byte_size);
          if (!MemAWrite(context, address + offset, data, addr_byte_size))
            return false;
        }

        // address = address + 4;
        offset += addr_byte_size;
      }
    }

    // if registers<15> == '1' then
    // MemA[address,4] = PCStoreValue();
    if (BitIsSet(registers, 15)) {
      RegisterInfo pc_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_pc, pc_reg);
      context.SetRegisterPlusOffset(pc_reg, 8);
      const uint32_t pc = ReadCoreReg(PC_REG, &success);
      if (!success)
        return false;

      if (!MemAWrite(context, address + offset, pc, addr_byte_size))
        return false;
    }

    // if wback then R[n] = R[n] + 4*BitCount(registers);
    if (wback) {
      offset = addr_byte_size * BitCount(registers);
      context.type = EmulateInstruction::eContextAdjustBaseRegister;
      context.SetImmediateSigned(offset);
      addr_t data = Rn + offset;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 data))
        return false;
    }
  }
  return true;
}

// STR (store immediate) calculates an address from a base register value and an
// immediate offset, and stores a word
// from a register to memory.  It can use offset, post-indexed, or pre-indexed
// addressing.
bool EmulateInstructionARM::EmulateSTRThumb(const uint32_t opcode,
                                            const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        if UnalignedSupport() || address<1:0> == '00' then
            MemU[address,4] = R[t]; 
        else // Can only occur before ARMv7
            MemU[address,4] = bits(32) UNKNOWN; 
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();

    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;
    // EncodingSpecificOperations (); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm5:'00', 32);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      imm32 = Bits32(opcode, 10, 6) << 2;

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = false;
      wback = false;
      break;

    case eEncodingT2:
      // t = UInt(Rt); n = 13; imm32 = ZeroExtend(imm8:'00', 32);
      t = Bits32(opcode, 10, 8);
      n = 13;
      imm32 = Bits32(opcode, 7, 0) << 2;

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;
      break;

    case eEncodingT3:
      // if Rn == '1111' then UNDEFINED;
      if (Bits32(opcode, 19, 16) == 15)
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // if t == 15 then UNPREDICTABLE;
      if (t == 15)
        return false;
      break;

    case eEncodingT4:
      // if P == '1' && U == '1' && W == '0' then SEE STRT;
      // if Rn == '1101' && P == '1' && U == '0' && W == '1' && imm8 ==
      // '00000100' then SEE PUSH;
      // if Rn == '1111' || (P == '0' && W == '0') then UNDEFINED;
      if ((Bits32(opcode, 19, 16) == 15) ||
          (BitIsClear(opcode, 10) && BitIsClear(opcode, 8)))
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0);

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 10);
      add = BitIsSet(opcode, 9);
      wback = BitIsSet(opcode, 8);

      // if t == 15 || (wback && n == t) then UNPREDICTABLE;
      if ((t == 15) || (wback && (n == t)))
        return false;
      break;

    default:
      return false;
    }

    addr_t offset_addr;
    addr_t address;

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    uint32_t base_address = ReadCoreReg(n, &success);
    if (!success)
      return false;

    if (add)
      offset_addr = base_address + imm32;
    else
      offset_addr = base_address - imm32;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = base_address;

    EmulateInstruction::Context context;
    if (n == 13)
      context.type = eContextPushRegisterOnStack;
    else
      context.type = eContextRegisterStore;

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    // if UnalignedSupport() || address<1:0> == '00' then
    if (UnalignedSupport() ||
        (BitIsClear(address, 1) && BitIsClear(address, 0))) {
      // MemU[address,4] = R[t];
      uint32_t data =
          ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + t, 0, &success);
      if (!success)
        return false;

      RegisterInfo data_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);
      int32_t offset = address - base_address;
      context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, offset);
      if (!MemUWrite(context, address, data, addr_byte_size))
        return false;
    } else {
      // MemU[address,4] = bits(32) UNKNOWN;
      WriteBits32UnknownToMemory(address);
    }

    // if wback then R[n] = offset_addr;
    if (wback) {
      if (n == 13)
        context.type = eContextAdjustStackPointer;
      else
        context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// STR (Store Register) calculates an address from a base register value and an
// offset register value, stores a
// word from a register to memory.   The offset register value can optionally
// be shifted.
bool EmulateInstructionARM::EmulateSTRRegister(const uint32_t opcode,
                                               const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        if t == 15 then // Only possible for encoding A1
            data = PCStoreValue(); 
        else
            data = R[t]; 
        if UnalignedSupport() || address<1:0> == '00' || CurrentInstrSet() == InstrSet_ARM then
            MemU[address,4] = data; 
        else // Can only occur before ARMv7
            MemU[address,4] = bits(32) UNKNOWN; 
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();

    uint32_t t;
    uint32_t n;
    uint32_t m;
    ARM_ShifterType shift_t;
    uint32_t shift_n;
    bool index;
    bool add;
    bool wback;

    // EncodingSpecificOperations (); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // if CurrentInstrSet() == InstrSet_ThumbEE then SEE "Modified operation
      // in ThumbEE";
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 8, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;
      break;

    case eEncodingT2:
      // if Rn == '1111' then UNDEFINED;
      if (Bits32(opcode, 19, 16) == 15)
        return false;

      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, UInt(imm2));
      shift_t = SRType_LSL;
      shift_n = Bits32(opcode, 5, 4);

      // if t == 15 || BadReg(m) then UNPREDICTABLE;
      if ((t == 15) || (BadReg(m)))
        return false;
      break;

    case eEncodingA1: {
      // if P == '0' && W == '1' then SEE STRT;
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = (BitIsClear(opcode, 24) || BitIsSet(opcode, 21));

      // (shift_t, shift_n) = DecodeImmShift(type, imm5);
      uint32_t typ = Bits32(opcode, 6, 5);
      uint32_t imm5 = Bits32(opcode, 11, 7);
      shift_n = DecodeImmShift(typ, imm5, shift_t);

      // if m == 15 then UNPREDICTABLE;
      if (m == 15)
        return false;

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;

      break;
    }
    default:
      return false;
    }

    addr_t offset_addr;
    addr_t address;
    int32_t offset = 0;

    addr_t base_address =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    uint32_t Rm_data =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    // offset = Shift(R[m], shift_t, shift_n, APSR.C);
    offset = Shift(Rm_data, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    // offset_addr = if add then (R[n] + offset) else (R[n] - offset);
    if (add)
      offset_addr = base_address + offset;
    else
      offset_addr = base_address - offset;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = base_address;

    uint32_t data;
    // if t == 15 then // Only possible for encoding A1
    if (t == 15)
      // data = PCStoreValue();
      data = ReadCoreReg(PC_REG, &success);
    else
      // data = R[t];
      data =
          ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + t, 0, &success);

    if (!success)
      return false;

    EmulateInstruction::Context context;
    context.type = eContextRegisterStore;

    // if UnalignedSupport() || address<1:0> == '00' || CurrentInstrSet() ==
    // InstrSet_ARM then
    if (UnalignedSupport() ||
        (BitIsClear(address, 1) && BitIsClear(address, 0)) ||
        CurrentInstrSet() == eModeARM) {
      // MemU[address,4] = data;

      RegisterInfo base_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

      RegisterInfo data_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);

      context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                              address - base_address);
      if (!MemUWrite(context, address, data, addr_byte_size))
        return false;

    } else
      // MemU[address,4] = bits(32) UNKNOWN;
      WriteBits32UnknownToMemory(address);

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextRegisterLoad;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

bool EmulateInstructionARM::EmulateSTRBThumb(const uint32_t opcode,
                                             const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        MemU[address,1] = R[t]<7:0>; 
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;
    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm5, 32);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      imm32 = Bits32(opcode, 10, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;
      break;

    case eEncodingT2:
      // if Rn == '1111' then UNDEFINED;
      if (Bits32(opcode, 19, 16) == 15)
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // if BadReg(t) then UNPREDICTABLE;
      if (BadReg(t))
        return false;
      break;

    case eEncodingT3:
      // if P == '1' && U == '1' && W == '0' then SEE STRBT;
      // if Rn == '1111' || (P == '0' && W == '0') then UNDEFINED;
      if (Bits32(opcode, 19, 16) == 15)
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0);

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 10);
      add = BitIsSet(opcode, 9);
      wback = BitIsSet(opcode, 8);

      // if BadReg(t) || (wback && n == t) then UNPREDICTABLE
      if ((BadReg(t)) || (wback && (n == t)))
        return false;
      break;

    default:
      return false;
    }

    addr_t offset_addr;
    addr_t address;
    addr_t base_address =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    if (add)
      offset_addr = base_address + imm32;
    else
      offset_addr = base_address - imm32;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = base_address;

    // MemU[address,1] = R[t]<7:0>
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    RegisterInfo data_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterStore;
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                            address - base_address);

    uint32_t data =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + t, 0, &success);
    if (!success)
      return false;

    data = Bits32(data, 7, 0);

    if (!MemUWrite(context, address, data, 1))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextRegisterLoad;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }

  return true;
}

// STRH (register) calculates an address from a base register value and an
// offset register value, and stores a
// halfword from a register to memory.  The offset register value can be
// shifted left by 0, 1, 2, or 3 bits.
bool EmulateInstructionARM::EmulateSTRHRegister(const uint32_t opcode,
                                                const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n);
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        if UnalignedSupport() || address<0> == '0' then
            MemU[address,2] = R[t]<15:0>; 
        else // Can only occur before ARMv7
            MemU[address,2] = bits(16) UNKNOWN; 
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t m;
    bool index;
    bool add;
    bool wback;
    ARM_ShifterType shift_t;
    uint32_t shift_n;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // if CurrentInstrSet() == InstrSet_ThumbEE then SEE "Modified operation
      // in ThumbEE";
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 8, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      break;

    case eEncodingT2:
      // if Rn == '1111' then UNDEFINED;
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);
      if (n == 15)
        return false;

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, UInt(imm2));
      shift_t = SRType_LSL;
      shift_n = Bits32(opcode, 5, 4);

      // if BadReg(t) || BadReg(m) then UNPREDICTABLE;
      if (BadReg(t) || BadReg(m))
        return false;

      break;

    case eEncodingA1:
      // if P == '0' && W == '1' then SEE STRHT;
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = (BitIsClear(opcode, 24) || BitIsSet(opcode, 21));

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      // if t == 15 || m == 15 then UNPREDICTABLE;
      if ((t == 15) || (m == 15))
        return false;

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;

      break;

    default:
      return false;
    }

    uint32_t Rm = ReadCoreReg(m, &success);
    if (!success)
      return false;

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // offset = Shift(R[m], shift_t, shift_n, APSR.C);
    uint32_t offset = Shift(Rm, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    // offset_addr = if add then (R[n] + offset) else (R[n] - offset);
    addr_t offset_addr;
    if (add)
      offset_addr = Rn + offset;
    else
      offset_addr = Rn - offset;

    // address = if index then offset_addr else R[n];
    addr_t address;
    if (index)
      address = offset_addr;
    else
      address = Rn;

    EmulateInstruction::Context context;
    context.type = eContextRegisterStore;
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
    RegisterInfo offset_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, offset_reg);

    // if UnalignedSupport() || address<0> == '0' then
    if (UnalignedSupport() || BitIsClear(address, 0)) {
      // MemU[address,2] = R[t]<15:0>;
      uint32_t Rt = ReadCoreReg(t, &success);
      if (!success)
        return false;

      EmulateInstruction::Context context;
      context.type = eContextRegisterStore;
      RegisterInfo base_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
      RegisterInfo offset_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, offset_reg);
      RegisterInfo data_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);
      context.SetRegisterToRegisterPlusIndirectOffset(base_reg, offset_reg,
                                                      data_reg);

      if (!MemUWrite(context, address, Bits32(Rt, 15, 0), 2))
        return false;
    } else // Can only occur before ARMv7
    {
      // MemU[address,2] = bits(16) UNKNOWN;
    }

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }

  return true;
}

// Add with Carry (immediate) adds an immediate value and the carry flag value
// to a register value, and writes the result to the destination register.  It
// can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateADCImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], imm32, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn;
    uint32_t
        imm32; // the immediate value to be added to the value obtained from Rn
    bool setflags;
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
      if (BadReg(Rd) || BadReg(Rn))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)

      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    int32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    AddWithCarryResult res = AddWithCarry(val1, imm32, APSR_C);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// Add with Carry (register) adds a register value, the carry flag value, and
// an optionally-shifted register value, and writes the result to the
// destination register.  It can optionally update the condition flags based on
// the result.
bool EmulateInstructionARM::EmulateADCReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], shifted, APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn, Rm;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    bool setflags;
    switch (encoding) {
    case eEncodingT1:
      Rd = Rn = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      shift_t = SRType_LSL;
      shift_n = 0;
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);
      if (BadReg(Rd) || BadReg(Rn) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftARM(opcode, shift_t);

      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    int32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // Read the second operand.
    int32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;
    AddWithCarryResult res = AddWithCarry(val1, shifted, APSR_C);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// This instruction adds an immediate value to the PC value to form a PC-
// relative address, and writes the result to the destination register.
bool EmulateInstructionARM::EmulateADR(const uint32_t opcode,
                                       const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = if add then (Align(PC,4) + imm32) else (Align(PC,4) - imm32);
        if d == 15 then         // Can only occur for ARM encodings
            ALUWritePC(result);
        else
            R[d] = result;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd;
    uint32_t imm32; // the immediate value to be added/subtracted to/from the PC
    bool add;
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 10, 8);
      imm32 = ThumbImm8Scaled(opcode); // imm32 = ZeroExtend(imm8:'00', 32)
      add = true;
      break;
    case eEncodingT2:
    case eEncodingT3:
      Rd = Bits32(opcode, 11, 8);
      imm32 = ThumbImm12(opcode); // imm32 = ZeroExtend(i:imm3:imm8, 32)
      add = (Bits32(opcode, 24, 21) == 0); // 0b0000 => ADD; 0b0101 => SUB
      if (BadReg(Rd))
        return false;
      break;
    case eEncodingA1:
    case eEncodingA2:
      Rd = Bits32(opcode, 15, 12);
      imm32 = ARMExpandImm(opcode);          // imm32 = ARMExpandImm(imm12)
      add = (Bits32(opcode, 24, 21) == 0x4); // 0b0100 => ADD; 0b0010 => SUB
      break;
    default:
      return false;
    }

    // Read the PC value.
    uint32_t pc = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;

    uint32_t result = (add ? Align(pc, 4) + imm32 : Align(pc, 4) - imm32);

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreReg(context, result, Rd))
      return false;
  }
  return true;
}

// This instruction performs a bitwise AND of a register value and an immediate
// value, and writes the result to the destination register.  It can optionally
// update the condition flags based on the result.
bool EmulateInstructionARM::EmulateANDImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] AND imm32;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn;
    uint32_t
        imm32; // the immediate value to be ANDed to the value obtained from Rn
    bool setflags;
    uint32_t carry; // the carry bit after ARM/Thumb Expand operation
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm_C(
          opcode, APSR_C,
          carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
      // if Rd == '1111' && S == '1' then SEE TST (immediate);
      if (Rd == 15 && setflags)
        return EmulateTSTImm(opcode, eEncodingT1);
      if (Rd == 13 || (Rd == 15 && !setflags) || BadReg(Rn))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 =
          ARMExpandImm_C(opcode, APSR_C,
                         carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)

      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    uint32_t result = val1 & imm32;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// This instruction performs a bitwise AND of a register value and an
// optionally-shifted register value, and writes the result to the destination
// register.  It can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateANDReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] AND shifted;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn, Rm;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    bool setflags;
    uint32_t carry;
    switch (encoding) {
    case eEncodingT1:
      Rd = Rn = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      shift_t = SRType_LSL;
      shift_n = 0;
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);
      // if Rd == '1111' && S == '1' then SEE TST (register);
      if (Rd == 15 && setflags)
        return EmulateTSTReg(opcode, eEncodingT2);
      if (Rd == 13 || (Rd == 15 && !setflags) || BadReg(Rn) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftARM(opcode, shift_t);

      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // Read the second operand.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry, &success);
    if (!success)
      return false;
    uint32_t result = val1 & shifted;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// Bitwise Bit Clear (immediate) performs a bitwise AND of a register value and
// the complement of an immediate value, and writes the result to the
// destination register.  It can optionally update the condition flags based on
// the result.
bool EmulateInstructionARM::EmulateBICImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] AND NOT(imm32);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn;
    uint32_t imm32; // the immediate value to be bitwise inverted and ANDed to
                    // the value obtained from Rn
    bool setflags;
    uint32_t carry; // the carry bit after ARM/Thumb Expand operation
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm_C(
          opcode, APSR_C,
          carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
      if (BadReg(Rd) || BadReg(Rn))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 =
          ARMExpandImm_C(opcode, APSR_C,
                         carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    uint32_t result = val1 & ~imm32;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// Bitwise Bit Clear (register) performs a bitwise AND of a register value and
// the complement of an optionally-shifted register value, and writes the
// result to the destination register. It can optionally update the condition
// flags based on the result.
bool EmulateInstructionARM::EmulateBICReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] AND NOT(shifted);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn, Rm;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    bool setflags;
    uint32_t carry;
    switch (encoding) {
    case eEncodingT1:
      Rd = Rn = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      shift_t = SRType_LSL;
      shift_n = 0;
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);
      if (BadReg(Rd) || BadReg(Rn) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftARM(opcode, shift_t);

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // Read the second operand.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry, &success);
    if (!success)
      return false;
    uint32_t result = val1 & ~shifted;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// LDR (immediate, ARM) calculates an address from a base register value and an
// immediate offset, loads a word
// from memory, and writes it to a register.  It can use offset, post-indexed,
// or pre-indexed addressing.
bool EmulateInstructionARM::EmulateLDRImmediateARM(const uint32_t opcode,
                                                   const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        data = MemU[address,4]; 
        if wback then R[n] = offset_addr; 
        if t == 15 then
            if address<1:0> == '00' then LoadWritePC(data); else UNPREDICTABLE; 
        elsif UnalignedSupport() || address<1:0> = '00' then
            R[t] = data; 
        else // Can only apply before ARMv7
            R[t] = ROR(data, 8*UInt(address<1:0>));
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();

    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    switch (encoding) {
    case eEncodingA1:
      // if Rn == '1111' then SEE LDR (literal);
      // if P == '0' && W == '1' then SEE LDRT;
      // if Rn == '1101' && P == '0' && U == '1' && W == '0' && imm12 ==
      // '000000000100' then SEE POP;
      // t == UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = (BitIsClear(opcode, 24) || BitIsSet(opcode, 21));

      // if wback && n == t then UNPREDICTABLE;
      if (wback && (n == t))
        return false;

      break;

    default:
      return false;
    }

    addr_t address;
    addr_t offset_addr;
    addr_t base_address = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    if (add)
      offset_addr = base_address + imm32;
    else
      offset_addr = base_address - imm32;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = base_address;

    // data = MemU[address,4];

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - base_address);

    uint64_t data = MemURead(context, address, addr_byte_size, 0, &success);
    if (!success)
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }

    // if t == 15 then
    if (t == 15) {
      // if address<1:0> == '00' then LoadWritePC(data); else UNPREDICTABLE;
      if (BitIsClear(address, 1) && BitIsClear(address, 0)) {
        // LoadWritePC (data);
        context.type = eContextRegisterLoad;
        context.SetRegisterPlusOffset(base_reg, address - base_address);
        LoadWritePC(context, data);
      } else
        return false;
    }
    // elsif UnalignedSupport() || address<1:0> = '00' then
    else if (UnalignedSupport() ||
             (BitIsClear(address, 1) && BitIsClear(address, 0))) {
      // R[t] = data;
      context.type = eContextRegisterLoad;
      context.SetRegisterPlusOffset(base_reg, address - base_address);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 data))
        return false;
    }
    // else // Can only apply before ARMv7
    else {
      // R[t] = ROR(data, 8*UInt(address<1:0>));
      data = ROR(data, Bits32(address, 1, 0), &success);
      if (!success)
        return false;
      context.type = eContextRegisterLoad;
      context.SetImmediate(data);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 data))
        return false;
    }
  }
  return true;
}

// LDR (register) calculates an address from a base register value and an offset
// register value, loads a word
// from memory, and writes it to a register.  The offset register value can
// optionally be shifted.
bool EmulateInstructionARM::EmulateLDRRegister(const uint32_t opcode,
                                               const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        data = MemU[address,4]; 
        if wback then R[n] = offset_addr; 
        if t == 15 then
            if address<1:0> == '00' then LoadWritePC(data); else UNPREDICTABLE; 
        elsif UnalignedSupport() || address<1:0> = '00' then
            R[t] = data; 
        else // Can only apply before ARMv7
            if CurrentInstrSet() == InstrSet_ARM then 
                R[t] = ROR(data, 8*UInt(address<1:0>));
            else 
                R[t] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    const uint32_t addr_byte_size = GetAddressByteSize();

    uint32_t t;
    uint32_t n;
    uint32_t m;
    bool index;
    bool add;
    bool wback;
    ARM_ShifterType shift_t;
    uint32_t shift_n;

    switch (encoding) {
    case eEncodingT1:
      // if CurrentInstrSet() == InstrSet_ThumbEE then SEE "Modified operation
      // in ThumbEE";
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 8, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      break;

    case eEncodingT2:
      // if Rn == '1111' then SEE LDR (literal);
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, UInt(imm2));
      shift_t = SRType_LSL;
      shift_n = Bits32(opcode, 5, 4);

      // if BadReg(m) then UNPREDICTABLE;
      if (BadReg(m))
        return false;

      // if t == 15 && InITBlock() && !LastInITBlock() then UNPREDICTABLE;
      if ((t == 15) && InITBlock() && !LastInITBlock())
        return false;

      break;

    case eEncodingA1: {
      // if P == '0' && W == '1' then SEE LDRT;
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = (BitIsClear(opcode, 24) || BitIsSet(opcode, 21));

      // (shift_t, shift_n) = DecodeImmShift(type, imm5);
      uint32_t type = Bits32(opcode, 6, 5);
      uint32_t imm5 = Bits32(opcode, 11, 7);
      shift_n = DecodeImmShift(type, imm5, shift_t);

      // if m == 15 then UNPREDICTABLE;
      if (m == 15)
        return false;

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;
    } break;

    default:
      return false;
    }

    uint32_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    uint32_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    addr_t address;

    // offset = Shift(R[m], shift_t, shift_n, APSR.C);   -- Note "The APSR is
    // an application level alias for the CPSR".
    addr_t offset =
        Shift(Rm, shift_t, shift_n, Bit32(m_opcode_cpsr, APSR_C), &success);
    if (!success)
      return false;

    // offset_addr = if add then (R[n] + offset) else (R[n] - offset);
    if (add)
      offset_addr = Rn + offset;
    else
      offset_addr = Rn - offset;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // data = MemU[address,4];
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - Rn);

    uint64_t data = MemURead(context, address, addr_byte_size, 0, &success);
    if (!success)
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }

    // if t == 15 then
    if (t == 15) {
      // if address<1:0> == '00' then LoadWritePC(data); else UNPREDICTABLE;
      if (BitIsClear(address, 1) && BitIsClear(address, 0)) {
        context.type = eContextRegisterLoad;
        context.SetRegisterPlusOffset(base_reg, address - Rn);
        LoadWritePC(context, data);
      } else
        return false;
    }
    // elsif UnalignedSupport() || address<1:0> = '00' then
    else if (UnalignedSupport() ||
             (BitIsClear(address, 1) && BitIsClear(address, 0))) {
      // R[t] = data;
      context.type = eContextRegisterLoad;
      context.SetRegisterPlusOffset(base_reg, address - Rn);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 data))
        return false;
    } else // Can only apply before ARMv7
    {
      // if CurrentInstrSet() == InstrSet_ARM then
      if (CurrentInstrSet() == eModeARM) {
        // R[t] = ROR(data, 8*UInt(address<1:0>));
        data = ROR(data, Bits32(address, 1, 0), &success);
        if (!success)
          return false;
        context.type = eContextRegisterLoad;
        context.SetImmediate(data);
        if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                   data))
          return false;
      } else {
        // R[t] = bits(32) UNKNOWN;
        WriteBits32Unknown(t);
      }
    }
  }
  return true;
}

// LDRB (immediate, Thumb)
bool EmulateInstructionARM::EmulateLDRBImmediate(const uint32_t opcode,
                                                 const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        R[t] = ZeroExtend(MemU[address,1], 32); 
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm5, 32);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      imm32 = Bits32(opcode, 10, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      break;

    case eEncodingT2:
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // if Rt == '1111' then SEE PLD;
      if (t == 15)
        return false; // PLD is not implemented yet

      // if Rn == '1111' then SEE LDRB (literal);
      if (n == 15)
        return EmulateLDRBLiteral(opcode, eEncodingT1);

      // if t == 13 then UNPREDICTABLE;
      if (t == 13)
        return false;

      break;

    case eEncodingT3:
      // if P == '1' && U == '1' && W == '0' then SEE LDRBT;
      // if P == '0' && W == '0' then UNDEFINED;
      if (BitIsClear(opcode, 10) && BitIsClear(opcode, 8))
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0);

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 10);
      add = BitIsSet(opcode, 9);
      wback = BitIsSet(opcode, 8);

      // if Rt == '1111' && P == '1' && U == '0' && W == '0' then SEE PLD;
      if (t == 15)
        return false; // PLD is not implemented yet

      // if Rn == '1111' then SEE LDRB (literal);
      if (n == 15)
        return EmulateLDRBLiteral(opcode, eEncodingT1);

      // if BadReg(t) || (wback && n == t) then UNPREDICTABLE;
      if (BadReg(t) || (wback && (n == t)))
        return false;

      break;

    default:
      return false;
    }

    uint32_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    addr_t address;
    addr_t offset_addr;

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    if (add)
      offset_addr = Rn + imm32;
    else
      offset_addr = Rn - imm32;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // R[t] = ZeroExtend(MemU[address,1], 32);
    RegisterInfo base_reg;
    RegisterInfo data_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, address - Rn);

    uint64_t data = MemURead(context, address, 1, 0, &success);
    if (!success)
      return false;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t, data))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// LDRB (literal) calculates an address from the PC value and an immediate
// offset, loads a byte from memory,
// zero-extends it to form a 32-bit word and writes it to a register.
bool EmulateInstructionARM::EmulateLDRBLiteral(const uint32_t opcode,
                                               const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(15); 
        base = Align(PC,4); 
        address = if add then (base + imm32) else (base - imm32); 
        R[t] = ZeroExtend(MemU[address,1], 32);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t imm32;
    bool add;
    switch (encoding) {
    case eEncodingT1:
      // t = UInt(Rt); imm32 = ZeroExtend(imm12, 32); add = (U == '1');
      t = Bits32(opcode, 15, 12);
      imm32 = Bits32(opcode, 11, 0);
      add = BitIsSet(opcode, 23);

      // if Rt == '1111' then SEE PLD;
      if (t == 15)
        return false; // PLD is not implemented yet

      // if t == 13 then UNPREDICTABLE;
      if (t == 13)
        return false;

      break;

    case eEncodingA1:
      // t == UInt(Rt); imm32 = ZeroExtend(imm12, 32); add = (U == '1');
      t = Bits32(opcode, 15, 12);
      imm32 = Bits32(opcode, 11, 0);
      add = BitIsSet(opcode, 23);

      // if t == 15 then UNPREDICTABLE;
      if (t == 15)
        return false;
      break;

    default:
      return false;
    }

    // base = Align(PC,4);
    uint32_t pc_val = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;

    uint32_t base = AlignPC(pc_val);

    addr_t address;
    // address = if add then (base + imm32) else (base - imm32);
    if (add)
      address = base + imm32;
    else
      address = base - imm32;

    // R[t] = ZeroExtend(MemU[address,1], 32);
    EmulateInstruction::Context context;
    context.type = eContextRelativeBranchImmediate;
    context.SetImmediate(address - base);

    uint64_t data = MemURead(context, address, 1, 0, &success);
    if (!success)
      return false;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t, data))
      return false;
  }
  return true;
}

// LDRB (register) calculates an address from a base register value and an
// offset rigister value, loads a byte from memory, zero-extends it to form a
// 32-bit word, and writes it to a register. The offset register value can
// optionally be shifted.
bool EmulateInstructionARM::EmulateLDRBRegister(const uint32_t opcode,
                                                const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        R[t] = ZeroExtend(MemU[address,1],32); 
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t m;
    bool index;
    bool add;
    bool wback;
    ARM_ShifterType shift_t;
    uint32_t shift_n;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 8, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;
      break;

    case eEncodingT2:
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, UInt(imm2));
      shift_t = SRType_LSL;
      shift_n = Bits32(opcode, 5, 4);

      // if Rt == '1111' then SEE PLD;
      if (t == 15)
        return false; // PLD is not implemented yet

      // if Rn == '1111' then SEE LDRB (literal);
      if (n == 15)
        return EmulateLDRBLiteral(opcode, eEncodingT1);

      // if t == 13 || BadReg(m) then UNPREDICTABLE;
      if ((t == 13) || BadReg(m))
        return false;
      break;

    case eEncodingA1: {
      // if P == '0' && W == '1' then SEE LDRBT;
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = (BitIsClear(opcode, 24) || BitIsSet(opcode, 21));

      // (shift_t, shift_n) = DecodeImmShift(type, imm5);
      uint32_t type = Bits32(opcode, 6, 5);
      uint32_t imm5 = Bits32(opcode, 11, 7);
      shift_n = DecodeImmShift(type, imm5, shift_t);

      // if t == 15 || m == 15 then UNPREDICTABLE;
      if ((t == 15) || (m == 15))
        return false;

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;
    } break;

    default:
      return false;
    }

    addr_t offset_addr;
    addr_t address;

    // offset = Shift(R[m], shift_t, shift_n, APSR.C);
    uint32_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    addr_t offset = Shift(Rm, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    // offset_addr = if add then (R[n] + offset) else (R[n] - offset);
    uint32_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    if (add)
      offset_addr = Rn + offset;
    else
      offset_addr = Rn - offset;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // R[t] = ZeroExtend(MemU[address,1],32);
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - Rn);

    uint64_t data = MemURead(context, address, 1, 0, &success);
    if (!success)
      return false;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t, data))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// LDRH (immediate, Thumb) calculates an address from a base register value and
// an immediate offset, loads a
// halfword from memory, zero-extends it to form a 32-bit word, and writes it
// to a register.  It can use offset, post-indexed, or pre-indexed addressing.
bool EmulateInstructionARM::EmulateLDRHImmediate(const uint32_t opcode,
                                                 const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        data = MemU[address,2]; 
        if wback then R[n] = offset_addr; 
        if UnalignedSupport() || address<0> = '0' then
            R[t] = ZeroExtend(data, 32); 
        else // Can only apply before ARMv7
            R[t] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm5:'0', 32);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      imm32 = Bits32(opcode, 10, 6) << 1;

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      break;

    case eEncodingT2:
      // if Rt == '1111' then SEE "Unallocated memory hints";
      // if Rn == '1111' then SEE LDRH (literal);
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // if t == 13 then UNPREDICTABLE;
      if (t == 13)
        return false;
      break;

    case eEncodingT3:
      // if Rn == '1111' then SEE LDRH (literal);
      // if Rt == '1111' && P == '1' && U == '0' && W == '0' then SEE
      // "Unallocated memory hints";
      // if P == '1' && U == '1' && W == '0' then SEE LDRHT;
      // if P == '0' && W == '0' then UNDEFINED;
      if (BitIsClear(opcode, 10) && BitIsClear(opcode, 8))
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0);

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 10);
      add = BitIsSet(opcode, 9);
      wback = BitIsSet(opcode, 8);

      // if BadReg(t) || (wback && n == t) then UNPREDICTABLE;
      if (BadReg(t) || (wback && (n == t)))
        return false;
      break;

    default:
      return false;
    }

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    uint32_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    addr_t address;

    if (add)
      offset_addr = Rn + imm32;
    else
      offset_addr = Rn - imm32;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // data = MemU[address,2];
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - Rn);

    uint64_t data = MemURead(context, address, 2, 0, &success);
    if (!success)
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }

    // if UnalignedSupport() || address<0> = '0' then
    if (UnalignedSupport() || BitIsClear(address, 0)) {
      // R[t] = ZeroExtend(data, 32);
      context.type = eContextRegisterLoad;
      context.SetRegisterPlusOffset(base_reg, address - Rn);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 data))
        return false;
    } else // Can only apply before ARMv7
    {
      // R[t] = bits(32) UNKNOWN;
      WriteBits32Unknown(t);
    }
  }
  return true;
}

// LDRH (literal) caculates an address from the PC value and an immediate
// offset, loads a halfword from memory,
// zero-extends it to form a 32-bit word, and writes it to a register.
bool EmulateInstructionARM::EmulateLDRHLiteral(const uint32_t opcode,
                                               const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(15); 
        base = Align(PC,4); 
        address = if add then (base + imm32) else (base - imm32); 
        data = MemU[address,2]; 
        if UnalignedSupport() || address<0> = '0' then
            R[t] = ZeroExtend(data, 32); 
        else // Can only apply before ARMv7
            R[t] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t imm32;
    bool add;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(15);
    switch (encoding) {
    case eEncodingT1:
      // if Rt == '1111' then SEE "Unallocated memory hints";
      // t = UInt(Rt); imm32 = ZeroExtend(imm12, 32); add = (U == '1');
      t = Bits32(opcode, 15, 12);
      imm32 = Bits32(opcode, 11, 0);
      add = BitIsSet(opcode, 23);

      // if t == 13 then UNPREDICTABLE;
      if (t == 13)
        return false;

      break;

    case eEncodingA1: {
      uint32_t imm4H = Bits32(opcode, 11, 8);
      uint32_t imm4L = Bits32(opcode, 3, 0);

      // t == UInt(Rt); imm32 = ZeroExtend(imm4H:imm4L, 32); add = (U == '1');
      t = Bits32(opcode, 15, 12);
      imm32 = (imm4H << 4) | imm4L;
      add = BitIsSet(opcode, 23);

      // if t == 15 then UNPREDICTABLE;
      if (t == 15)
        return false;
      break;
    }

    default:
      return false;
    }

    // base = Align(PC,4);
    uint64_t pc_value = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;

    addr_t base = AlignPC(pc_value);
    addr_t address;

    // address = if add then (base + imm32) else (base - imm32);
    if (add)
      address = base + imm32;
    else
      address = base - imm32;

    // data = MemU[address,2];
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - base);

    uint64_t data = MemURead(context, address, 2, 0, &success);
    if (!success)
      return false;

    // if UnalignedSupport() || address<0> = '0' then
    if (UnalignedSupport() || BitIsClear(address, 0)) {
      // R[t] = ZeroExtend(data, 32);
      context.type = eContextRegisterLoad;
      context.SetRegisterPlusOffset(base_reg, address - base);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 data))
        return false;

    } else // Can only apply before ARMv7
    {
      // R[t] = bits(32) UNKNOWN;
      WriteBits32Unknown(t);
    }
  }
  return true;
}

// LDRH (literal) calculates an address from a base register value and an offset
// register value, loads a halfword
// from memory, zero-extends it to form a 32-bit word, and writes it to a
// register.  The offset register value can be shifted left by 0, 1, 2, or 3
// bits.
bool EmulateInstructionARM::EmulateLDRHRegister(const uint32_t opcode,
                                                const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        data = MemU[address,2]; 
        if wback then R[n] = offset_addr; 
        if UnalignedSupport() || address<0> = '0' then
            R[t] = ZeroExtend(data, 32); 
        else // Can only apply before ARMv7
            R[t] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t m;
    bool index;
    bool add;
    bool wback;
    ARM_ShifterType shift_t;
    uint32_t shift_n;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // if CurrentInstrSet() == InstrSet_ThumbEE then SEE "Modified operation
      // in ThumbEE";
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 8, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      break;

    case eEncodingT2:
      // if Rn == '1111' then SEE LDRH (literal);
      // if Rt == '1111' then SEE "Unallocated memory hints";
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, UInt(imm2));
      shift_t = SRType_LSL;
      shift_n = Bits32(opcode, 5, 4);

      // if t == 13 || BadReg(m) then UNPREDICTABLE;
      if ((t == 13) || BadReg(m))
        return false;
      break;

    case eEncodingA1:
      // if P == '0' && W == '1' then SEE LDRHT;
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = (BitIsClear(opcode, 24) || BitIsSet(opcode, 21));

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      // if t == 15 || m == 15 then UNPREDICTABLE;
      if ((t == 15) || (m == 15))
        return false;

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;

      break;

    default:
      return false;
    }

    // offset = Shift(R[m], shift_t, shift_n, APSR.C);

    uint64_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    addr_t offset = Shift(Rm, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    addr_t address;

    // offset_addr = if add then (R[n] + offset) else (R[n] - offset);
    uint64_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    if (add)
      offset_addr = Rn + offset;
    else
      offset_addr = Rn - offset;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // data = MemU[address,2];
    RegisterInfo base_reg;
    RegisterInfo offset_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, offset_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusIndirectOffset(base_reg, offset_reg);
    uint64_t data = MemURead(context, address, 2, 0, &success);
    if (!success)
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }

    // if UnalignedSupport() || address<0> = '0' then
    if (UnalignedSupport() || BitIsClear(address, 0)) {
      // R[t] = ZeroExtend(data, 32);
      context.type = eContextRegisterLoad;
      context.SetRegisterPlusIndirectOffset(base_reg, offset_reg);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 data))
        return false;
    } else // Can only apply before ARMv7
    {
      // R[t] = bits(32) UNKNOWN;
      WriteBits32Unknown(t);
    }
  }
  return true;
}

// LDRSB (immediate) calculates an address from a base register value and an
// immediate offset, loads a byte from
// memory, sign-extends it to form a 32-bit word, and writes it to a register.
// It can use offset, post-indexed, or pre-indexed addressing.
bool EmulateInstructionARM::EmulateLDRSBImmediate(const uint32_t opcode,
                                                  const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        R[t] = SignExtend(MemU[address,1], 32); 
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // if Rt == '1111' then SEE PLI;
      // if Rn == '1111' then SEE LDRSB (literal);
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // if t == 13 then UNPREDICTABLE;
      if (t == 13)
        return false;

      break;

    case eEncodingT2:
      // if Rt == '1111' && P == '1' && U == '0' && W == '0' then SEE PLI;
      // if Rn == '1111' then SEE LDRSB (literal);
      // if P == '1' && U == '1' && W == '0' then SEE LDRSBT;
      // if P == '0' && W == '0' then UNDEFINED;
      if (BitIsClear(opcode, 10) && BitIsClear(opcode, 8))
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0);

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 10);
      add = BitIsSet(opcode, 9);
      wback = BitIsSet(opcode, 8);

      // if BadReg(t) || (wback && n == t) then UNPREDICTABLE;
      if (((t == 13) ||
           ((t == 15) && (BitIsClear(opcode, 10) || BitIsSet(opcode, 9) ||
                          BitIsSet(opcode, 8)))) ||
          (wback && (n == t)))
        return false;

      break;

    case eEncodingA1: {
      // if Rn == '1111' then SEE LDRSB (literal);
      // if P == '0' && W == '1' then SEE LDRSBT;
      // t == UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm4H:imm4L, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);

      uint32_t imm4H = Bits32(opcode, 11, 8);
      uint32_t imm4L = Bits32(opcode, 3, 0);
      imm32 = (imm4H << 4) | imm4L;

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = (BitIsClear(opcode, 24) || BitIsSet(opcode, 21));

      // if t == 15 || (wback && n == t) then UNPREDICTABLE;
      if ((t == 15) || (wback && (n == t)))
        return false;

      break;
    }

    default:
      return false;
    }

    uint64_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    addr_t address;

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    if (add)
      offset_addr = Rn + imm32;
    else
      offset_addr = Rn - imm32;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // R[t] = SignExtend(MemU[address,1], 32);
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - Rn);

    uint64_t unsigned_data = MemURead(context, address, 1, 0, &success);
    if (!success)
      return false;

    int64_t signed_data = llvm::SignExtend64<8>(unsigned_data);
    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                               (uint64_t)signed_data))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }

  return true;
}

// LDRSB (literal) calculates an address from the PC value and an immediate
// offset, loads a byte from memory,
// sign-extends it to form a 32-bit word, and writes tit to a register.
bool EmulateInstructionARM::EmulateLDRSBLiteral(const uint32_t opcode,
                                                const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(15); 
        base = Align(PC,4); 
        address = if add then (base + imm32) else (base - imm32); 
        R[t] = SignExtend(MemU[address,1], 32);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t imm32;
    bool add;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(15);
    switch (encoding) {
    case eEncodingT1:
      // if Rt == '1111' then SEE PLI;
      // t = UInt(Rt); imm32 = ZeroExtend(imm12, 32); add = (U == '1');
      t = Bits32(opcode, 15, 12);
      imm32 = Bits32(opcode, 11, 0);
      add = BitIsSet(opcode, 23);

      // if t == 13 then UNPREDICTABLE;
      if (t == 13)
        return false;

      break;

    case eEncodingA1: {
      // t == UInt(Rt); imm32 = ZeroExtend(imm4H:imm4L, 32); add = (U == '1');
      t = Bits32(opcode, 15, 12);
      uint32_t imm4H = Bits32(opcode, 11, 8);
      uint32_t imm4L = Bits32(opcode, 3, 0);
      imm32 = (imm4H << 4) | imm4L;
      add = BitIsSet(opcode, 23);

      // if t == 15 then UNPREDICTABLE;
      if (t == 15)
        return false;

      break;
    }

    default:
      return false;
    }

    // base = Align(PC,4);
    uint64_t pc_value = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;
    uint64_t base = AlignPC(pc_value);

    // address = if add then (base + imm32) else (base - imm32);
    addr_t address;
    if (add)
      address = base + imm32;
    else
      address = base - imm32;

    // R[t] = SignExtend(MemU[address,1], 32);
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - base);

    uint64_t unsigned_data = MemURead(context, address, 1, 0, &success);
    if (!success)
      return false;

    int64_t signed_data = llvm::SignExtend64<8>(unsigned_data);
    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                               (uint64_t)signed_data))
      return false;
  }
  return true;
}

// LDRSB (register) calculates an address from a base register value and an
// offset register value, loadsa byte from
// memory, sign-extends it to form a 32-bit word, and writes it to a register.
// The offset register value can be shifted left by 0, 1, 2, or 3 bits.
bool EmulateInstructionARM::EmulateLDRSBRegister(const uint32_t opcode,
                                                 const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        R[t] = SignExtend(MemU[address,1], 32); 
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t m;
    bool index;
    bool add;
    bool wback;
    ARM_ShifterType shift_t;
    uint32_t shift_n;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 8, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      break;

    case eEncodingT2:
      // if Rt == '1111' then SEE PLI;
      // if Rn == '1111' then SEE LDRSB (literal);
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, UInt(imm2));
      shift_t = SRType_LSL;
      shift_n = Bits32(opcode, 5, 4);

      // if t == 13 || BadReg(m) then UNPREDICTABLE;
      if ((t == 13) || BadReg(m))
        return false;
      break;

    case eEncodingA1:
      // if P == '0' && W == '1' then SEE LDRSBT;
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      // if t == 15 || m == 15 then UNPREDICTABLE;
      if ((t == 15) || (m == 15))
        return false;

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;
      break;

    default:
      return false;
    }

    uint64_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    // offset = Shift(R[m], shift_t, shift_n, APSR.C);
    addr_t offset = Shift(Rm, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    addr_t address;

    // offset_addr = if add then (R[n] + offset) else (R[n] - offset);
    uint64_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    if (add)
      offset_addr = Rn + offset;
    else
      offset_addr = Rn - offset;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // R[t] = SignExtend(MemU[address,1], 32);
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
    RegisterInfo offset_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, offset_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusIndirectOffset(base_reg, offset_reg);

    uint64_t unsigned_data = MemURead(context, address, 1, 0, &success);
    if (!success)
      return false;

    int64_t signed_data = llvm::SignExtend64<8>(unsigned_data);
    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                               (uint64_t)signed_data))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// LDRSH (immediate) calculates an address from a base register value and an
// immediate offset, loads a halfword from
// memory, sign-extends it to form a 32-bit word, and writes it to a register.
// It can use offset, post-indexed, or pre-indexed addressing.
bool EmulateInstructionARM::EmulateLDRSHImmediate(const uint32_t opcode,
                                                  const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32); 
        address = if index then offset_addr else R[n]; 
        data = MemU[address,2]; 
        if wback then R[n] = offset_addr; 
        if UnalignedSupport() || address<0> = '0' then
            R[t] = SignExtend(data, 32); 
        else // Can only apply before ARMv7
            R[t] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // if Rn == '1111' then SEE LDRSH (literal);
      // if Rt == '1111' then SEE "Unallocated memory hints";
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // if t == 13 then UNPREDICTABLE;
      if (t == 13)
        return false;

      break;

    case eEncodingT2:
      // if Rn == '1111' then SEE LDRSH (literal);
      // if Rt == '1111' && P == '1' && U == '0' && W == '0' then SEE
      // "Unallocated memory hints";
      // if P == '1' && U == '1' && W == '0' then SEE LDRSHT;
      // if P == '0' && W == '0' then UNDEFINED;
      if (BitIsClear(opcode, 10) && BitIsClear(opcode, 8))
        return false;

      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm8, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0);

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 10);
      add = BitIsSet(opcode, 9);
      wback = BitIsSet(opcode, 8);

      // if BadReg(t) || (wback && n == t) then UNPREDICTABLE;
      if (BadReg(t) || (wback && (n == t)))
        return false;

      break;

    case eEncodingA1: {
      // if Rn == '1111' then SEE LDRSH (literal);
      // if P == '0' && W == '1' then SEE LDRSHT;
      // t == UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm4H:imm4L, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      uint32_t imm4H = Bits32(opcode, 11, 8);
      uint32_t imm4L = Bits32(opcode, 3, 0);
      imm32 = (imm4H << 4) | imm4L;

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // if t == 15 || (wback && n == t) then UNPREDICTABLE;
      if ((t == 15) || (wback && (n == t)))
        return false;

      break;
    }

    default:
      return false;
    }

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    uint64_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    if (add)
      offset_addr = Rn + imm32;
    else
      offset_addr = Rn - imm32;

    // address = if index then offset_addr else R[n];
    addr_t address;
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // data = MemU[address,2];
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - Rn);

    uint64_t data = MemURead(context, address, 2, 0, &success);
    if (!success)
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }

    // if UnalignedSupport() || address<0> = '0' then
    if (UnalignedSupport() || BitIsClear(address, 0)) {
      // R[t] = SignExtend(data, 32);
      int64_t signed_data = llvm::SignExtend64<16>(data);
      context.type = eContextRegisterLoad;
      context.SetRegisterPlusOffset(base_reg, address - Rn);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 (uint64_t)signed_data))
        return false;
    } else // Can only apply before ARMv7
    {
      // R[t] = bits(32) UNKNOWN;
      WriteBits32Unknown(t);
    }
  }
  return true;
}

// LDRSH (literal) calculates an address from the PC value and an immediate
// offset, loads a halfword from memory,
// sign-extends it to from a 32-bit word, and writes it to a register.
bool EmulateInstructionARM::EmulateLDRSHLiteral(const uint32_t opcode,
                                                const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(15); 
        base = Align(PC,4); 
        address = if add then (base + imm32) else (base - imm32); 
        data = MemU[address,2]; 
        if UnalignedSupport() || address<0> = '0' then
            R[t] = SignExtend(data, 32);
        else // Can only apply before ARMv7
            R[t] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t imm32;
    bool add;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(15);
    switch (encoding) {
    case eEncodingT1:
      // if Rt == '1111' then SEE "Unallocated memory hints";
      // t = UInt(Rt); imm32 = ZeroExtend(imm12, 32); add = (U == '1');
      t = Bits32(opcode, 15, 12);
      imm32 = Bits32(opcode, 11, 0);
      add = BitIsSet(opcode, 23);

      // if t == 13 then UNPREDICTABLE;
      if (t == 13)
        return false;

      break;

    case eEncodingA1: {
      // t == UInt(Rt); imm32 = ZeroExtend(imm4H:imm4L, 32); add = (U == '1');
      t = Bits32(opcode, 15, 12);
      uint32_t imm4H = Bits32(opcode, 11, 8);
      uint32_t imm4L = Bits32(opcode, 3, 0);
      imm32 = (imm4H << 4) | imm4L;
      add = BitIsSet(opcode, 23);

      // if t == 15 then UNPREDICTABLE;
      if (t == 15)
        return false;

      break;
    }
    default:
      return false;
    }

    // base = Align(PC,4);
    uint64_t pc_value = ReadCoreReg(PC_REG, &success);
    if (!success)
      return false;

    uint64_t base = AlignPC(pc_value);

    addr_t address;
    // address = if add then (base + imm32) else (base - imm32);
    if (add)
      address = base + imm32;
    else
      address = base - imm32;

    // data = MemU[address,2];
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC, base_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, imm32);

    uint64_t data = MemURead(context, address, 2, 0, &success);
    if (!success)
      return false;

    // if UnalignedSupport() || address<0> = '0' then
    if (UnalignedSupport() || BitIsClear(address, 0)) {
      // R[t] = SignExtend(data, 32);
      int64_t signed_data = llvm::SignExtend64<16>(data);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 (uint64_t)signed_data))
        return false;
    } else // Can only apply before ARMv7
    {
      // R[t] = bits(32) UNKNOWN;
      WriteBits32Unknown(t);
    }
  }
  return true;
}

// LDRSH (register) calculates an address from a base register value and an
// offset register value, loads a halfword
// from memory, sign-extends it to form a 32-bit word, and writes it to a
// register.  The offset register value can be shifted left by 0, 1, 2, or 3
// bits.
bool EmulateInstructionARM::EmulateLDRSHRegister(const uint32_t opcode,
                                                 const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); NullCheckIfThumbEE(n); 
        offset = Shift(R[m], shift_t, shift_n, APSR.C); 
        offset_addr = if add then (R[n] + offset) else (R[n] - offset); 
        address = if index then offset_addr else R[n]; 
        data = MemU[address,2]; 
        if wback then R[n] = offset_addr; 
        if UnalignedSupport() || address<0> = '0' then
            R[t] = SignExtend(data, 32); 
        else // Can only apply before ARMv7
            R[t] = bits(32) UNKNOWN;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t m;
    bool index;
    bool add;
    bool wback;
    ARM_ShifterType shift_t;
    uint32_t shift_n;

    // EncodingSpecificOperations(); NullCheckIfThumbEE(n);
    switch (encoding) {
    case eEncodingT1:
      // if CurrentInstrSet() == InstrSet_ThumbEE then SEE "Modified operation
      // in ThumbEE";
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 8, 6);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      break;

    case eEncodingT2:
      // if Rn == '1111' then SEE LDRSH (literal);
      // if Rt == '1111' then SEE "Unallocated memory hints";
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = TRUE; add = TRUE; wback = FALSE;
      index = true;
      add = true;
      wback = false;

      // (shift_t, shift_n) = (SRType_LSL, UInt(imm2));
      shift_t = SRType_LSL;
      shift_n = Bits32(opcode, 5, 4);

      // if t == 13 || BadReg(m) then UNPREDICTABLE;
      if ((t == 13) || BadReg(m))
        return false;

      break;

    case eEncodingA1:
      // if P == '0' && W == '1' then SEE LDRSHT;
      // t = UInt(Rt); n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1');     add = (U == '1');       wback = (P == '0') ||
      // (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      // if t == 15 || m == 15 then UNPREDICTABLE;
      if ((t == 15) || (m == 15))
        return false;

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;

      break;

    default:
      return false;
    }

    uint64_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    uint64_t Rn =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
    if (!success)
      return false;

    // offset = Shift(R[m], shift_t, shift_n, APSR.C);
    addr_t offset = Shift(Rm, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    addr_t address;

    // offset_addr = if add then (R[n] + offset) else (R[n] - offset);
    if (add)
      offset_addr = Rn + offset;
    else
      offset_addr = Rn - offset;

    // address = if index then offset_addr else R[n];
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // data = MemU[address,2];
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    RegisterInfo offset_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, offset_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusIndirectOffset(base_reg, offset_reg);

    uint64_t data = MemURead(context, address, 2, 0, &success);
    if (!success)
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }

    // if UnalignedSupport() || address<0> = '0' then
    if (UnalignedSupport() || BitIsClear(address, 0)) {
      // R[t] = SignExtend(data, 32);
      context.type = eContextRegisterLoad;
      context.SetRegisterPlusIndirectOffset(base_reg, offset_reg);

      int64_t signed_data = llvm::SignExtend64<16>(data);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t,
                                 (uint64_t)signed_data))
        return false;
    } else // Can only apply before ARMv7
    {
      // R[t] = bits(32) UNKNOWN;
      WriteBits32Unknown(t);
    }
  }
  return true;
}

// SXTB extracts an 8-bit value from a register, sign-extends it to 32 bits, and
// writes the result to the destination
// register.  You can specifiy a rotation by 0, 8, 16, or 24 bits before
// extracting the 8-bit value.
bool EmulateInstructionARM::EmulateSXTB(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations();
        rotated = ROR(R[m], rotation); 
        R[d] = SignExtend(rotated<7:0>, 32);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t m;
    uint32_t rotation;

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); m = UInt(Rm); rotation = 0;
      d = Bits32(opcode, 2, 0);
      m = Bits32(opcode, 5, 3);
      rotation = 0;

      break;

    case eEncodingT2:
      // d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');
      d = Bits32(opcode, 11, 8);
      m = Bits32(opcode, 3, 0);
      rotation = Bits32(opcode, 5, 4) << 3;

      // if BadReg(d) || BadReg(m) then UNPREDICTABLE;
      if (BadReg(d) || BadReg(m))
        return false;

      break;

    case eEncodingA1:
      // d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');
      d = Bits32(opcode, 15, 12);
      m = Bits32(opcode, 3, 0);
      rotation = Bits32(opcode, 11, 10) << 3;

      // if d == 15 || m == 15 then UNPREDICTABLE;
      if ((d == 15) || (m == 15))
        return false;

      break;

    default:
      return false;
    }

    uint64_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    // rotated = ROR(R[m], rotation);
    uint64_t rotated = ROR(Rm, rotation, &success);
    if (!success)
      return false;

    // R[d] = SignExtend(rotated<7:0>, 32);
    int64_t data = llvm::SignExtend64<8>(rotated);

    RegisterInfo source_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, source_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegister(source_reg);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + d,
                               (uint64_t)data))
      return false;
  }
  return true;
}

// SXTH extracts a 16-bit value from a register, sign-extends it to 32 bits, and
// writes the result to the destination
// register.  You can specify a rotation by 0, 8, 16, or 24 bits before
// extracting the 16-bit value.
bool EmulateInstructionARM::EmulateSXTH(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        rotated = ROR(R[m], rotation); 
        R[d] = SignExtend(rotated<15:0>, 32);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t m;
    uint32_t rotation;

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); m = UInt(Rm); rotation = 0;
      d = Bits32(opcode, 2, 0);
      m = Bits32(opcode, 5, 3);
      rotation = 0;

      break;

    case eEncodingT2:
      // d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');
      d = Bits32(opcode, 11, 8);
      m = Bits32(opcode, 3, 0);
      rotation = Bits32(opcode, 5, 4) << 3;

      // if BadReg(d) || BadReg(m) then UNPREDICTABLE;
      if (BadReg(d) || BadReg(m))
        return false;

      break;

    case eEncodingA1:
      // d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');
      d = Bits32(opcode, 15, 12);
      m = Bits32(opcode, 3, 0);
      rotation = Bits32(opcode, 11, 10) << 3;

      // if d == 15 || m == 15 then UNPREDICTABLE;
      if ((d == 15) || (m == 15))
        return false;

      break;

    default:
      return false;
    }

    uint64_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    // rotated = ROR(R[m], rotation);
    uint64_t rotated = ROR(Rm, rotation, &success);
    if (!success)
      return false;

    // R[d] = SignExtend(rotated<15:0>, 32);
    RegisterInfo source_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, source_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegister(source_reg);

    int64_t data = llvm::SignExtend64<16>(rotated);
    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + d,
                               (uint64_t)data))
      return false;
  }

  return true;
}

// UXTB extracts an 8-bit value from a register, zero-extneds it to 32 bits, and
// writes the result to the destination
// register.  You can specify a rotation by 0, 8, 16, or 24 bits before
// extracting the 8-bit value.
bool EmulateInstructionARM::EmulateUXTB(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        rotated = ROR(R[m], rotation); 
        R[d] = ZeroExtend(rotated<7:0>, 32);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t m;
    uint32_t rotation;

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); m = UInt(Rm); rotation = 0;
      d = Bits32(opcode, 2, 0);
      m = Bits32(opcode, 5, 3);
      rotation = 0;

      break;

    case eEncodingT2:
      // d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');
      d = Bits32(opcode, 11, 8);
      m = Bits32(opcode, 3, 0);
      rotation = Bits32(opcode, 5, 4) << 3;

      // if BadReg(d) || BadReg(m) then UNPREDICTABLE;
      if (BadReg(d) || BadReg(m))
        return false;

      break;

    case eEncodingA1:
      // d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');
      d = Bits32(opcode, 15, 12);
      m = Bits32(opcode, 3, 0);
      rotation = Bits32(opcode, 11, 10) << 3;

      // if d == 15 || m == 15 then UNPREDICTABLE;
      if ((d == 15) || (m == 15))
        return false;

      break;

    default:
      return false;
    }

    uint64_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    // rotated = ROR(R[m], rotation);
    uint64_t rotated = ROR(Rm, rotation, &success);
    if (!success)
      return false;

    // R[d] = ZeroExtend(rotated<7:0>, 32);
    RegisterInfo source_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, source_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegister(source_reg);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + d,
                               Bits32(rotated, 7, 0)))
      return false;
  }
  return true;
}

// UXTH extracts a 16-bit value from a register, zero-extends it to 32 bits, and
// writes the result to the destination
// register.  You can specify a rotation by 0, 8, 16, or 24 bits before
// extracting the 16-bit value.
bool EmulateInstructionARM::EmulateUXTH(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        rotated = ROR(R[m], rotation); 
        R[d] = ZeroExtend(rotated<15:0>, 32);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t m;
    uint32_t rotation;

    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); m = UInt(Rm); rotation = 0;
      d = Bits32(opcode, 2, 0);
      m = Bits32(opcode, 5, 3);
      rotation = 0;

      break;

    case eEncodingT2:
      // d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');
      d = Bits32(opcode, 11, 8);
      m = Bits32(opcode, 3, 0);
      rotation = Bits32(opcode, 5, 4) << 3;

      // if BadReg(d) || BadReg(m) then UNPREDICTABLE;
      if (BadReg(d) || BadReg(m))
        return false;

      break;

    case eEncodingA1:
      // d = UInt(Rd); m = UInt(Rm); rotation = UInt(rotate:'000');
      d = Bits32(opcode, 15, 12);
      m = Bits32(opcode, 3, 0);
      rotation = Bits32(opcode, 11, 10) << 3;

      // if d == 15 || m == 15 then UNPREDICTABLE;
      if ((d == 15) || (m == 15))
        return false;

      break;

    default:
      return false;
    }

    uint64_t Rm =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + m, 0, &success);
    if (!success)
      return false;

    // rotated = ROR(R[m], rotation);
    uint64_t rotated = ROR(Rm, rotation, &success);
    if (!success)
      return false;

    // R[d] = ZeroExtend(rotated<15:0>, 32);
    RegisterInfo source_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, source_reg);

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegister(source_reg);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + d,
                               Bits32(rotated, 15, 0)))
      return false;
  }
  return true;
}

// RFE (Return From Exception) loads the PC and the CPSR from the word at the
// specified address and the following
// word respectively.
bool EmulateInstructionARM::EmulateRFE(const uint32_t opcode,
                                       const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then 
        EncodingSpecificOperations(); 
        if !CurrentModeIsPrivileged() || CurrentInstrSet() == InstrSet_ThumbEE then
            UNPREDICTABLE; 
        else
            address = if increment then R[n] else R[n]-8; 
            if wordhigher then address = address+4; 
            CPSRWriteByInstr(MemA[address+4,4], '1111', TRUE); 
            BranchWritePC(MemA[address,4]);
            if wback then R[n] = if increment then R[n]+8 else R[n]-8;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    bool wback;
    bool increment;
    bool wordhigher;

    // EncodingSpecificOperations();
    switch (encoding) {
    case eEncodingT1:
      // n = UInt(Rn); wback = (W == '1'); increment = FALSE; wordhigher =
      // FALSE;
      n = Bits32(opcode, 19, 16);
      wback = BitIsSet(opcode, 21);
      increment = false;
      wordhigher = false;

      // if n == 15 then UNPREDICTABLE;
      if (n == 15)
        return false;

      // if InITBlock() && !LastInITBlock() then UNPREDICTABLE;
      if (InITBlock() && !LastInITBlock())
        return false;

      break;

    case eEncodingT2:
      // n = UInt(Rn); wback = (W == '1'); increment = TRUE; wordhigher = FALSE;
      n = Bits32(opcode, 19, 16);
      wback = BitIsSet(opcode, 21);
      increment = true;
      wordhigher = false;

      // if n == 15 then UNPREDICTABLE;
      if (n == 15)
        return false;

      // if InITBlock() && !LastInITBlock() then UNPREDICTABLE;
      if (InITBlock() && !LastInITBlock())
        return false;

      break;

    case eEncodingA1:
      // n = UInt(Rn);
      n = Bits32(opcode, 19, 16);

      // wback = (W == '1'); inc = (U == '1'); wordhigher = (P == U);
      wback = BitIsSet(opcode, 21);
      increment = BitIsSet(opcode, 23);
      wordhigher = (Bit32(opcode, 24) == Bit32(opcode, 23));

      // if n == 15 then UNPREDICTABLE;
      if (n == 15)
        return false;

      break;

    default:
      return false;
    }

    // if !CurrentModeIsPrivileged() || CurrentInstrSet() == InstrSet_ThumbEE
    // then
    if (!CurrentModeIsPrivileged())
      // UNPREDICTABLE;
      return false;
    else {
      uint64_t Rn =
          ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + n, 0, &success);
      if (!success)
        return false;

      addr_t address;
      // address = if increment then R[n] else R[n]-8;
      if (increment)
        address = Rn;
      else
        address = Rn - 8;

      // if wordhigher then address = address+4;
      if (wordhigher)
        address = address + 4;

      // CPSRWriteByInstr(MemA[address+4,4], '1111', TRUE);
      RegisterInfo base_reg;
      GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

      EmulateInstruction::Context context;
      context.type = eContextReturnFromException;
      context.SetRegisterPlusOffset(base_reg, address - Rn);

      uint64_t data = MemARead(context, address + 4, 4, 0, &success);
      if (!success)
        return false;

      CPSRWriteByInstr(data, 15, true);

      // BranchWritePC(MemA[address,4]);
      uint64_t data2 = MemARead(context, address, 4, 0, &success);
      if (!success)
        return false;

      BranchWritePC(context, data2);

      // if wback then R[n] = if increment then R[n]+8 else R[n]-8;
      if (wback) {
        context.type = eContextAdjustBaseRegister;
        if (increment) {
          context.SetOffset(8);
          if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                     Rn + 8))
            return false;
        } else {
          context.SetOffset(-8);
          if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                     Rn - 8))
            return false;
        }
      } // if wback
    }
  } // if ConditionPassed()
  return true;
}

// Bitwise Exclusive OR (immediate) performs a bitwise exclusive OR of a
// register value and an immediate value, and writes the result to the
// destination register.  It can optionally update the condition flags based on
// the result.
bool EmulateInstructionARM::EmulateEORImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] EOR imm32;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn;
    uint32_t
        imm32; // the immediate value to be ORed to the value obtained from Rn
    bool setflags;
    uint32_t carry; // the carry bit after ARM/Thumb Expand operation
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm_C(
          opcode, APSR_C,
          carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
      // if Rd == '1111' && S == '1' then SEE TEQ (immediate);
      if (Rd == 15 && setflags)
        return EmulateTEQImm(opcode, eEncodingT1);
      if (Rd == 13 || (Rd == 15 && !setflags) || BadReg(Rn))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 =
          ARMExpandImm_C(opcode, APSR_C,
                         carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    uint32_t result = val1 ^ imm32;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// Bitwise Exclusive OR (register) performs a bitwise exclusive OR of a
// register value and an optionally-shifted register value, and writes the
// result to the destination register. It can optionally update the condition
// flags based on the result.
bool EmulateInstructionARM::EmulateEORReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] EOR shifted;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn, Rm;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    bool setflags;
    uint32_t carry;
    switch (encoding) {
    case eEncodingT1:
      Rd = Rn = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      shift_t = SRType_LSL;
      shift_n = 0;
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);
      // if Rd == '1111' && S == '1' then SEE TEQ (register);
      if (Rd == 15 && setflags)
        return EmulateTEQReg(opcode, eEncodingT1);
      if (Rd == 13 || (Rd == 15 && !setflags) || BadReg(Rn) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftARM(opcode, shift_t);

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // Read the second operand.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry, &success);
    if (!success)
      return false;
    uint32_t result = val1 ^ shifted;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// Bitwise OR (immediate) performs a bitwise (inclusive) OR of a register value
// and an immediate value, and writes the result to the destination register.
// It can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateORRImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] OR imm32;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn;
    uint32_t
        imm32; // the immediate value to be ORed to the value obtained from Rn
    bool setflags;
    uint32_t carry; // the carry bit after ARM/Thumb Expand operation
    switch (encoding) {
    case eEncodingT1:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ThumbExpandImm_C(
          opcode, APSR_C,
          carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
      // if Rn == '1111' then SEE MOV (immediate);
      if (Rn == 15)
        return EmulateMOVRdImm(opcode, eEncodingT2);
      if (BadReg(Rd) || Rn == 13)
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 =
          ARMExpandImm_C(opcode, APSR_C,
                         carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)

      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    uint32_t result = val1 | imm32;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// Bitwise OR (register) performs a bitwise (inclusive) OR of a register value
// and an optionally-shifted register value, and writes the result to the
// destination register.  It can optionally update the condition flags based on
// the result.
bool EmulateInstructionARM::EmulateORRReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] OR shifted;
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd, Rn, Rm;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    bool setflags;
    uint32_t carry;
    switch (encoding) {
    case eEncodingT1:
      Rd = Rn = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      setflags = !InITBlock();
      shift_t = SRType_LSL;
      shift_n = 0;
      break;
    case eEncodingT2:
      Rd = Bits32(opcode, 11, 8);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);
      // if Rn == '1111' then SEE MOV (register);
      if (Rn == 15)
        return EmulateMOVRdRm(opcode, eEncodingT3);
      if (BadReg(Rd) || Rn == 13 || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);
      shift_n = DecodeImmShiftARM(opcode, shift_t);

      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // Read the second operand.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry, &success);
    if (!success)
      return false;
    uint32_t result = val1 | shifted;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteCoreRegOptionalFlags(context, result, Rd, setflags, carry))
      return false;
  }
  return true;
}

// Reverse Subtract (immediate) subtracts a register value from an immediate
// value, and writes the result to the destination register. It can optionally
// update the condition flags based on the result.
bool EmulateInstructionARM::EmulateRSBImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(NOT(R[n]), imm32, '1');
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rd; // the destination register
  uint32_t Rn; // the first operand
  bool setflags;
  uint32_t
      imm32; // the immediate value to be added to the value obtained from Rn
  switch (encoding) {
  case eEncodingT1:
    Rd = Bits32(opcode, 2, 0);
    Rn = Bits32(opcode, 5, 3);
    setflags = !InITBlock();
    imm32 = 0;
    break;
  case eEncodingT2:
    Rd = Bits32(opcode, 11, 8);
    Rn = Bits32(opcode, 19, 16);
    setflags = BitIsSet(opcode, 20);
    imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
    if (BadReg(Rd) || BadReg(Rn))
      return false;
    break;
  case eEncodingA1:
    Rd = Bits32(opcode, 15, 12);
    Rn = Bits32(opcode, 19, 16);
    setflags = BitIsSet(opcode, 20);
    imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)

    // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
    // instructions;
    if (Rd == 15 && setflags)
      return EmulateSUBSPcLrEtc(opcode, encoding);
    break;
  default:
    return false;
  }
  // Read the register value from the operand register Rn.
  uint32_t reg_val = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  AddWithCarryResult res = AddWithCarry(~reg_val, imm32, 1);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();

  return WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow);
}

// Reverse Subtract (register) subtracts a register value from an optionally-
// shifted register value, and writes the result to the destination register.
// It can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateRSBReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(NOT(R[n]), shifted, '1');
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rd; // the destination register
  uint32_t Rn; // the first operand
  uint32_t Rm; // the second operand
  bool setflags;
  ARM_ShifterType shift_t;
  uint32_t shift_n; // the shift applied to the value read from Rm
  switch (encoding) {
  case eEncodingT1:
    Rd = Bits32(opcode, 11, 8);
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    setflags = BitIsSet(opcode, 20);
    shift_n = DecodeImmShiftThumb(opcode, shift_t);
    // if (BadReg(d) || BadReg(m)) then UNPREDICTABLE;
    if (BadReg(Rd) || BadReg(Rn) || BadReg(Rm))
      return false;
    break;
  case eEncodingA1:
    Rd = Bits32(opcode, 15, 12);
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    setflags = BitIsSet(opcode, 20);
    shift_n = DecodeImmShiftARM(opcode, shift_t);

    // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
    // instructions;
    if (Rd == 15 && setflags)
      return EmulateSUBSPcLrEtc(opcode, encoding);
    break;
  default:
    return false;
  }
  // Read the register value from register Rn.
  uint32_t val1 = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  // Read the register value from register Rm.
  uint32_t val2 = ReadCoreReg(Rm, &success);
  if (!success)
    return false;

  uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C, &success);
  if (!success)
    return false;
  AddWithCarryResult res = AddWithCarry(~val1, shifted, 1);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();
  return WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow);
}

// Reverse Subtract with Carry (immediate) subtracts a register value and the
// value of NOT (Carry flag) from an immediate value, and writes the result to
// the destination register. It can optionally update the condition flags based
// on the result.
bool EmulateInstructionARM::EmulateRSCImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(NOT(R[n]), imm32, APSR.C);
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rd; // the destination register
  uint32_t Rn; // the first operand
  bool setflags;
  uint32_t
      imm32; // the immediate value to be added to the value obtained from Rn
  switch (encoding) {
  case eEncodingA1:
    Rd = Bits32(opcode, 15, 12);
    Rn = Bits32(opcode, 19, 16);
    setflags = BitIsSet(opcode, 20);
    imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)

    // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
    // instructions;
    if (Rd == 15 && setflags)
      return EmulateSUBSPcLrEtc(opcode, encoding);
    break;
  default:
    return false;
  }
  // Read the register value from the operand register Rn.
  uint32_t reg_val = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  AddWithCarryResult res = AddWithCarry(~reg_val, imm32, APSR_C);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();

  return WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow);
}

// Reverse Subtract with Carry (register) subtracts a register value and the
// value of NOT (Carry flag) from an optionally-shifted register value, and
// writes the result to the destination register. It can optionally update the
// condition flags based on the result.
bool EmulateInstructionARM::EmulateRSCReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(NOT(R[n]), shifted, APSR.C);
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rd; // the destination register
  uint32_t Rn; // the first operand
  uint32_t Rm; // the second operand
  bool setflags;
  ARM_ShifterType shift_t;
  uint32_t shift_n; // the shift applied to the value read from Rm
  switch (encoding) {
  case eEncodingA1:
    Rd = Bits32(opcode, 15, 12);
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    setflags = BitIsSet(opcode, 20);
    shift_n = DecodeImmShiftARM(opcode, shift_t);

    // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
    // instructions;
    if (Rd == 15 && setflags)
      return EmulateSUBSPcLrEtc(opcode, encoding);
    break;
  default:
    return false;
  }
  // Read the register value from register Rn.
  uint32_t val1 = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  // Read the register value from register Rm.
  uint32_t val2 = ReadCoreReg(Rm, &success);
  if (!success)
    return false;

  uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C, &success);
  if (!success)
    return false;
  AddWithCarryResult res = AddWithCarry(~val1, shifted, APSR_C);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();
  return WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow);
}

// Subtract with Carry (immediate) subtracts an immediate value and the value
// of
// NOT (Carry flag) from a register value, and writes the result to the
// destination register.
// It can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateSBCImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], NOT(imm32), APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rd; // the destination register
  uint32_t Rn; // the first operand
  bool setflags;
  uint32_t
      imm32; // the immediate value to be added to the value obtained from Rn
  switch (encoding) {
  case eEncodingT1:
    Rd = Bits32(opcode, 11, 8);
    Rn = Bits32(opcode, 19, 16);
    setflags = BitIsSet(opcode, 20);
    imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)
    if (BadReg(Rd) || BadReg(Rn))
      return false;
    break;
  case eEncodingA1:
    Rd = Bits32(opcode, 15, 12);
    Rn = Bits32(opcode, 19, 16);
    setflags = BitIsSet(opcode, 20);
    imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)

    // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
    // instructions;
    if (Rd == 15 && setflags)
      return EmulateSUBSPcLrEtc(opcode, encoding);
    break;
  default:
    return false;
  }
  // Read the register value from the operand register Rn.
  uint32_t reg_val = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  AddWithCarryResult res = AddWithCarry(reg_val, ~imm32, APSR_C);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();

  return WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow);
}

// Subtract with Carry (register) subtracts an optionally-shifted register
// value and the value of
// NOT (Carry flag) from a register value, and writes the result to the
// destination register.
// It can optionally update the condition flags based on the result.
bool EmulateInstructionARM::EmulateSBCReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], NOT(shifted), APSR.C);
        if d == 15 then         // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rd; // the destination register
  uint32_t Rn; // the first operand
  uint32_t Rm; // the second operand
  bool setflags;
  ARM_ShifterType shift_t;
  uint32_t shift_n; // the shift applied to the value read from Rm
  switch (encoding) {
  case eEncodingT1:
    Rd = Rn = Bits32(opcode, 2, 0);
    Rm = Bits32(opcode, 5, 3);
    setflags = !InITBlock();
    shift_t = SRType_LSL;
    shift_n = 0;
    break;
  case eEncodingT2:
    Rd = Bits32(opcode, 11, 8);
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    setflags = BitIsSet(opcode, 20);
    shift_n = DecodeImmShiftThumb(opcode, shift_t);
    if (BadReg(Rd) || BadReg(Rn) || BadReg(Rm))
      return false;
    break;
  case eEncodingA1:
    Rd = Bits32(opcode, 15, 12);
    Rn = Bits32(opcode, 19, 16);
    Rm = Bits32(opcode, 3, 0);
    setflags = BitIsSet(opcode, 20);
    shift_n = DecodeImmShiftARM(opcode, shift_t);

    // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
    // instructions;
    if (Rd == 15 && setflags)
      return EmulateSUBSPcLrEtc(opcode, encoding);
    break;
  default:
    return false;
  }
  // Read the register value from register Rn.
  uint32_t val1 = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  // Read the register value from register Rm.
  uint32_t val2 = ReadCoreReg(Rm, &success);
  if (!success)
    return false;

  uint32_t shifted = Shift(val2, shift_t, shift_n, APSR_C, &success);
  if (!success)
    return false;
  AddWithCarryResult res = AddWithCarry(val1, ~shifted, APSR_C);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();
  return WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow);
}

// This instruction subtracts an immediate value from a register value, and
// writes the result to the destination register.  It can optionally update the
// condition flags based on the result.
bool EmulateInstructionARM::EmulateSUBImmThumb(const uint32_t opcode,
                                               const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], NOT(imm32), '1');
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            APSR.V = overflow;
#endif

  bool success = false;

  uint32_t Rd; // the destination register
  uint32_t Rn; // the first operand
  bool setflags;
  uint32_t imm32; // the immediate value to be subtracted from the value
                  // obtained from Rn
  switch (encoding) {
  case eEncodingT1:
    Rd = Bits32(opcode, 2, 0);
    Rn = Bits32(opcode, 5, 3);
    setflags = !InITBlock();
    imm32 = Bits32(opcode, 8, 6); // imm32 = ZeroExtend(imm3, 32)
    break;
  case eEncodingT2:
    Rd = Rn = Bits32(opcode, 10, 8);
    setflags = !InITBlock();
    imm32 = Bits32(opcode, 7, 0); // imm32 = ZeroExtend(imm8, 32)
    break;
  case eEncodingT3:
    Rd = Bits32(opcode, 11, 8);
    Rn = Bits32(opcode, 19, 16);
    setflags = BitIsSet(opcode, 20);
    imm32 = ThumbExpandImm(opcode); // imm32 = ThumbExpandImm(i:imm3:imm8)

    // if Rd == '1111' && S == '1' then SEE CMP (immediate);
    if (Rd == 15 && setflags)
      return EmulateCMPImm(opcode, eEncodingT2);

    // if Rn == '1101' then SEE SUB (SP minus immediate);
    if (Rn == 13)
      return EmulateSUBSPImm(opcode, eEncodingT2);

    // if d == 13 || (d == 15 && S == '0') || n == 15 then UNPREDICTABLE;
    if (Rd == 13 || (Rd == 15 && !setflags) || Rn == 15)
      return false;
    break;
  case eEncodingT4:
    Rd = Bits32(opcode, 11, 8);
    Rn = Bits32(opcode, 19, 16);
    setflags = BitIsSet(opcode, 20);
    imm32 = ThumbImm12(opcode); // imm32 = ZeroExtend(i:imm3:imm8, 32)

    // if Rn == '1111' then SEE ADR;
    if (Rn == 15)
      return EmulateADR(opcode, eEncodingT2);

    // if Rn == '1101' then SEE SUB (SP minus immediate);
    if (Rn == 13)
      return EmulateSUBSPImm(opcode, eEncodingT3);

    if (BadReg(Rd))
      return false;
    break;
  default:
    return false;
  }
  // Read the register value from the operand register Rn.
  uint32_t reg_val = ReadCoreReg(Rn, &success);
  if (!success)
    return false;

  AddWithCarryResult res = AddWithCarry(reg_val, ~imm32, 1);

  EmulateInstruction::Context context;
  context.type = EmulateInstruction::eContextImmediate;
  context.SetNoArgs();

  return WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow);
}

// This instruction subtracts an immediate value from a register value, and
// writes the result to the destination register.  It can optionally update the
// condition flags based on the result.
bool EmulateInstructionARM::EmulateSUBImmARM(const uint32_t opcode,
                                             const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (result, carry, overflow) = AddWithCarry(R[n], NOT(imm32), '1');
        if d == 15 then
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rd; // the destination register
    uint32_t Rn; // the first operand
    bool setflags;
    uint32_t imm32; // the immediate value to be subtracted from the value
                    // obtained from Rn
    switch (encoding) {
    case eEncodingA1:
      Rd = Bits32(opcode, 15, 12);
      Rn = Bits32(opcode, 19, 16);
      setflags = BitIsSet(opcode, 20);
      imm32 = ARMExpandImm(opcode); // imm32 = ARMExpandImm(imm12)

      // if Rn == '1111' && S == '0' then SEE ADR;
      if (Rn == 15 && !setflags)
        return EmulateADR(opcode, eEncodingA2);

      // if Rn == '1101' then SEE SUB (SP minus immediate);
      if (Rn == 13)
        return EmulateSUBSPImm(opcode, eEncodingA1);

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (Rd == 15 && setflags)
        return EmulateSUBSPcLrEtc(opcode, encoding);
      break;
    default:
      return false;
    }
    // Read the register value from the operand register Rn.
    uint32_t reg_val = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    AddWithCarryResult res = AddWithCarry(reg_val, ~imm32, 1);

    EmulateInstruction::Context context;
    if (Rd == 13)
      context.type = EmulateInstruction::eContextAdjustStackPointer;
    else
      context.type = EmulateInstruction::eContextRegisterPlusOffset;

    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, Rn, dwarf_reg);
    int64_t imm32_signed = imm32;
    context.SetRegisterPlusOffset(dwarf_reg, -imm32_signed);

    if (!WriteCoreRegOptionalFlags(context, res.result, Rd, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// Test Equivalence (immediate) performs a bitwise exclusive OR operation on a
// register value and an immediate value.  It updates the condition flags based
// on the result, and discards the result.
bool EmulateInstructionARM::EmulateTEQImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] EOR imm32;
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rn;
    uint32_t
        imm32; // the immediate value to be ANDed to the value obtained from Rn
    uint32_t carry; // the carry bit after ARM/Thumb Expand operation
    switch (encoding) {
    case eEncodingT1:
      Rn = Bits32(opcode, 19, 16);
      imm32 = ThumbExpandImm_C(
          opcode, APSR_C,
          carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
      if (BadReg(Rn))
        return false;
      break;
    case eEncodingA1:
      Rn = Bits32(opcode, 19, 16);
      imm32 =
          ARMExpandImm_C(opcode, APSR_C,
                         carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    uint32_t result = val1 ^ imm32;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteFlags(context, result, carry))
      return false;
  }
  return true;
}

// Test Equivalence (register) performs a bitwise exclusive OR operation on a
// register value and an optionally-shifted register value.  It updates the
// condition flags based on the result, and discards the result.
bool EmulateInstructionARM::EmulateTEQReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] EOR shifted;
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rn, Rm;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    uint32_t carry;
    switch (encoding) {
    case eEncodingT1:
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);
      if (BadReg(Rn) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      shift_n = DecodeImmShiftARM(opcode, shift_t);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // Read the second operand.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry, &success);
    if (!success)
      return false;
    uint32_t result = val1 ^ shifted;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteFlags(context, result, carry))
      return false;
  }
  return true;
}

// Test (immediate) performs a bitwise AND operation on a register value and an
// immediate value. It updates the condition flags based on the result, and
// discards the result.
bool EmulateInstructionARM::EmulateTSTImm(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        result = R[n] AND imm32;
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rn;
    uint32_t
        imm32; // the immediate value to be ANDed to the value obtained from Rn
    uint32_t carry; // the carry bit after ARM/Thumb Expand operation
    switch (encoding) {
    case eEncodingT1:
      Rn = Bits32(opcode, 19, 16);
      imm32 = ThumbExpandImm_C(
          opcode, APSR_C,
          carry); // (imm32, carry) = ThumbExpandImm(i:imm3:imm8, APSR.C)
      if (BadReg(Rn))
        return false;
      break;
    case eEncodingA1:
      Rn = Bits32(opcode, 19, 16);
      imm32 =
          ARMExpandImm_C(opcode, APSR_C,
                         carry); // (imm32, carry) = ARMExpandImm(imm12, APSR.C)
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    uint32_t result = val1 & imm32;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteFlags(context, result, carry))
      return false;
  }
  return true;
}

// Test (register) performs a bitwise AND operation on a register value and an
// optionally-shifted register value. It updates the condition flags based on
// the result, and discards the result.
bool EmulateInstructionARM::EmulateTSTReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    // ARM pseudo code...
    if ConditionPassed() then
        EncodingSpecificOperations();
        (shifted, carry) = Shift_C(R[m], shift_t, shift_n, APSR.C);
        result = R[n] AND shifted;
        APSR.N = result<31>;
        APSR.Z = IsZeroBit(result);
        APSR.C = carry;
        // APSR.V unchanged
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t Rn, Rm;
    ARM_ShifterType shift_t;
    uint32_t shift_n; // the shift applied to the value read from Rm
    uint32_t carry;
    switch (encoding) {
    case eEncodingT1:
      Rn = Bits32(opcode, 2, 0);
      Rm = Bits32(opcode, 5, 3);
      shift_t = SRType_LSL;
      shift_n = 0;
      break;
    case eEncodingT2:
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);
      if (BadReg(Rn) || BadReg(Rm))
        return false;
      break;
    case eEncodingA1:
      Rn = Bits32(opcode, 19, 16);
      Rm = Bits32(opcode, 3, 0);
      shift_n = DecodeImmShiftARM(opcode, shift_t);
      break;
    default:
      return false;
    }

    // Read the first operand.
    uint32_t val1 = ReadCoreReg(Rn, &success);
    if (!success)
      return false;

    // Read the second operand.
    uint32_t val2 = ReadCoreReg(Rm, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift_C(val2, shift_t, shift_n, APSR_C, carry, &success);
    if (!success)
      return false;
    uint32_t result = val1 & shifted;

    EmulateInstruction::Context context;
    context.type = EmulateInstruction::eContextImmediate;
    context.SetNoArgs();

    if (!WriteFlags(context, result, carry))
      return false;
  }
  return true;
}

// A8.6.216 SUB (SP minus register)
bool EmulateInstructionARM::EmulateSUBSPReg(const uint32_t opcode,
                                            const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(SP, NOT(shifted), '1');
        if d == 15 then // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t m;
    bool setflags;
    ARM_ShifterType shift_t;
    uint32_t shift_n;

    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); m = UInt(Rm); setflags = (S == '1');
      d = Bits32(opcode, 11, 8);
      m = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);

      // (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);

      // if d == 13 && (shift_t != SRType_LSL || shift_n > 3) then
      // UNPREDICTABLE;
      if ((d == 13) && ((shift_t != SRType_LSL) || (shift_n > 3)))
        return false;

      // if d == 15 || BadReg(m) then UNPREDICTABLE;
      if ((d == 15) || BadReg(m))
        return false;
      break;

    case eEncodingA1:
      // d = UInt(Rd); m = UInt(Rm); setflags = (S == '1');
      d = Bits32(opcode, 15, 12);
      m = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if (d == 15 && setflags)
        EmulateSUBSPcLrEtc(opcode, encoding);

      // (shift_t, shift_n) = DecodeImmShift(type, imm5);
      shift_n = DecodeImmShiftARM(opcode, shift_t);
      break;

    default:
      return false;
    }

    // shifted = Shift(R[m], shift_t, shift_n, APSR.C);
    uint32_t Rm = ReadCoreReg(m, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift(Rm, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    // (result, carry, overflow) = AddWithCarry(SP, NOT(shifted), '1');
    uint32_t sp_val = ReadCoreReg(SP_REG, &success);
    if (!success)
      return false;

    AddWithCarryResult res = AddWithCarry(sp_val, ~shifted, 1);

    EmulateInstruction::Context context;
    context.type = eContextArithmetic;
    RegisterInfo sp_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_sp, sp_reg);
    RegisterInfo dwarf_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, dwarf_reg);
    context.SetRegisterRegisterOperands(sp_reg, dwarf_reg);

    if (!WriteCoreRegOptionalFlags(context, res.result, dwarf_r0 + d, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// A8.6.7 ADD (register-shifted register)
bool EmulateInstructionARM::EmulateADDRegShift(const uint32_t opcode,
                                               const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations();
        shift_n = UInt(R[s]<7:0>);
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], shifted, '0');
        R[d] = result;
        if setflags then
            APSR.N = result<31>;
            APSR.Z = IsZeroBit(result);
            APSR.C = carry;
            APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t n;
    uint32_t m;
    uint32_t s;
    bool setflags;
    ARM_ShifterType shift_t;

    switch (encoding) {
    case eEncodingA1:
      // d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); s = UInt(Rs);
      d = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);
      s = Bits32(opcode, 11, 8);

      // setflags = (S == '1'); shift_t = DecodeRegShift(type);
      setflags = BitIsSet(opcode, 20);
      shift_t = DecodeRegShift(Bits32(opcode, 6, 5));

      // if d == 15 || n == 15 || m == 15 || s == 15 then UNPREDICTABLE;
      if ((d == 15) || (m == 15) || (m == 15) || (s == 15))
        return false;
      break;

    default:
      return false;
    }

    // shift_n = UInt(R[s]<7:0>);
    uint32_t Rs = ReadCoreReg(s, &success);
    if (!success)
      return false;

    uint32_t shift_n = Bits32(Rs, 7, 0);

    // shifted = Shift(R[m], shift_t, shift_n, APSR.C);
    uint32_t Rm = ReadCoreReg(m, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift(Rm, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    // (result, carry, overflow) = AddWithCarry(R[n], shifted, '0');
    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    AddWithCarryResult res = AddWithCarry(Rn, shifted, 0);

    // R[d] = result;
    EmulateInstruction::Context context;
    context.type = eContextArithmetic;
    RegisterInfo reg_n;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, reg_n);
    RegisterInfo reg_m;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, reg_m);

    context.SetRegisterRegisterOperands(reg_n, reg_m);

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + d,
                               res.result))
      return false;

    // if setflags then
    // APSR.N = result<31>;
    // APSR.Z = IsZeroBit(result);
    // APSR.C = carry;
    // APSR.V = overflow;
    if (setflags)
      return WriteFlags(context, res.result, res.carry_out, res.overflow);
  }
  return true;
}

// A8.6.213 SUB (register)
bool EmulateInstructionARM::EmulateSUBReg(const uint32_t opcode,
                                          const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations();
        shifted = Shift(R[m], shift_t, shift_n, APSR.C);
        (result, carry, overflow) = AddWithCarry(R[n], NOT(shifted), '1');
        if d == 15 then // Can only occur for ARM encoding
            ALUWritePC(result); // setflags is always FALSE here
        else
            R[d] = result;
            if setflags then
                APSR.N = result<31>;
                APSR.Z = IsZeroBit(result);
                APSR.C = carry;
                APSR.V = overflow;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t n;
    uint32_t m;
    bool setflags;
    ARM_ShifterType shift_t;
    uint32_t shift_n;

    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = !InITBlock();
      d = Bits32(opcode, 2, 0);
      n = Bits32(opcode, 5, 3);
      m = Bits32(opcode, 8, 6);
      setflags = !InITBlock();

      // (shift_t, shift_n) = (SRType_LSL, 0);
      shift_t = SRType_LSL;
      shift_n = 0;

      break;

    case eEncodingT2:
      // d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S =="1");
      d = Bits32(opcode, 11, 8);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);

      // if Rd == "1111" && S == "1" then SEE CMP (register);
      if (d == 15 && setflags == 1)
        return EmulateCMPImm(opcode, eEncodingT3);

      // if Rn == "1101" then SEE SUB (SP minus register);
      if (n == 13)
        return EmulateSUBSPReg(opcode, eEncodingT1);

      // (shift_t, shift_n) = DecodeImmShift(type, imm3:imm2);
      shift_n = DecodeImmShiftThumb(opcode, shift_t);

      // if d == 13 || (d == 15 && S == '0') || n == 15 || BadReg(m) then
      // UNPREDICTABLE;
      if ((d == 13) || ((d == 15) && BitIsClear(opcode, 20)) || (n == 15) ||
          BadReg(m))
        return false;

      break;

    case eEncodingA1:
      // if Rn == '1101' then SEE SUB (SP minus register);
      // d = UInt(Rd); n = UInt(Rn); m = UInt(Rm); setflags = (S == '1');
      d = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);
      setflags = BitIsSet(opcode, 20);

      // if Rd == '1111' && S == '1' then SEE SUBS PC, LR and related
      // instructions;
      if ((d == 15) && setflags)
        EmulateSUBSPcLrEtc(opcode, encoding);

      // (shift_t, shift_n) = DecodeImmShift(type, imm5);
      shift_n = DecodeImmShiftARM(opcode, shift_t);

      break;

    default:
      return false;
    }

    // shifted = Shift(R[m], shift_t, shift_n, APSR.C);
    uint32_t Rm = ReadCoreReg(m, &success);
    if (!success)
      return false;

    uint32_t shifted = Shift(Rm, shift_t, shift_n, APSR_C, &success);
    if (!success)
      return false;

    // (result, carry, overflow) = AddWithCarry(R[n], NOT(shifted), '1');
    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    AddWithCarryResult res = AddWithCarry(Rn, ~shifted, 1);

    // if d == 15 then // Can only occur for ARM encoding ALUWritePC(result);
    // // setflags is always FALSE here else
    // R[d] = result;
    // if setflags then
    // APSR.N = result<31>;
    // APSR.Z = IsZeroBit(result);
    // APSR.C = carry;
    // APSR.V = overflow;

    EmulateInstruction::Context context;
    context.type = eContextArithmetic;
    RegisterInfo reg_n;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, reg_n);
    RegisterInfo reg_m;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, reg_m);
    context.SetRegisterRegisterOperands(reg_n, reg_m);

    if (!WriteCoreRegOptionalFlags(context, res.result, dwarf_r0 + d, setflags,
                                   res.carry_out, res.overflow))
      return false;
  }
  return true;
}

// A8.6.202 STREX
// Store Register Exclusive calculates an address from a base register value
// and an immediate offset, and stores a word from a register to memory if the
// executing processor has exclusive access to the memory addressed.
bool EmulateInstructionARM::EmulateSTREX(const uint32_t opcode,
                                         const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); NullCheckIfThumbEE(n);
        address = R[n] + imm32;
        if ExclusiveMonitorsPass(address,4) then
            MemA[address,4] = R[t];
            R[d] = 0;
        else
            R[d] = 1;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t d;
    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    const uint32_t addr_byte_size = GetAddressByteSize();

    switch (encoding) {
    case eEncodingT1:
      // d = UInt(Rd); t = UInt(Rt); n = UInt(Rn); imm32 =
      // ZeroExtend(imm8:'00',
      // 32);
      d = Bits32(opcode, 11, 8);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // if BadReg(d) || BadReg(t) || n == 15 then UNPREDICTABLE;
      if (BadReg(d) || BadReg(t) || (n == 15))
        return false;

      // if d == n || d == t then UNPREDICTABLE;
      if ((d == n) || (d == t))
        return false;

      break;

    case eEncodingA1:
      // d = UInt(Rd); t = UInt(Rt); n = UInt(Rn); imm32 = Zeros(32); // Zero
      // offset
      d = Bits32(opcode, 15, 12);
      t = Bits32(opcode, 3, 0);
      n = Bits32(opcode, 19, 16);
      imm32 = 0;

      // if d == 15 || t == 15 || n == 15 then UNPREDICTABLE;
      if ((d == 15) || (t == 15) || (n == 15))
        return false;

      // if d == n || d == t then UNPREDICTABLE;
      if ((d == n) || (d == t))
        return false;

      break;

    default:
      return false;
    }

    // address = R[n] + imm32;
    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    addr_t address = Rn + imm32;

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
    RegisterInfo data_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);
    EmulateInstruction::Context context;
    context.type = eContextRegisterStore;
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, imm32);

    // if ExclusiveMonitorsPass(address,4) then if (ExclusiveMonitorsPass
    // (address, addr_byte_size)) -- For now, for the sake of emulation, we
    // will say this
    //                                                         always return
    //                                                         true.
    if (true) {
      // MemA[address,4] = R[t];
      uint32_t Rt =
          ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_r0 + t, 0, &success);
      if (!success)
        return false;

      if (!MemAWrite(context, address, Rt, addr_byte_size))
        return false;

      // R[d] = 0;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t, 0))
        return false;
    }
#if 0  // unreachable because if true
        else
        {
            // R[d] = 1;
            if (!WriteRegisterUnsigned (context, eRegisterKindDWARF, dwarf_r0 + t, 1))
                return false;
        }
#endif // unreachable because if true
  }
  return true;
}

// A8.6.197 STRB (immediate, ARM)
bool EmulateInstructionARM::EmulateSTRBImmARM(const uint32_t opcode,
                                              const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations();
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        MemU[address,1] = R[t]<7:0>;
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    switch (encoding) {
    case eEncodingA1:
      // if P == '0' && W == '1' then SEE STRBT;
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = (P == '1'); add = (U == '1'); wback = (P == '0') || (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // if t == 15 then UNPREDICTABLE;
      if (t == 15)
        return false;

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;

      break;

    default:
      return false;
    }

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    if (add)
      offset_addr = Rn + imm32;
    else
      offset_addr = Rn - imm32;

    // address = if index then offset_addr else R[n];
    addr_t address;
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // MemU[address,1] = R[t]<7:0>;
    uint32_t Rt = ReadCoreReg(t, &success);
    if (!success)
      return false;

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
    RegisterInfo data_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);
    EmulateInstruction::Context context;
    context.type = eContextRegisterStore;
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, address - Rn);

    if (!MemUWrite(context, address, Bits32(Rt, 7, 0), 1))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// A8.6.194 STR (immediate, ARM)
bool EmulateInstructionARM::EmulateSTRImmARM(const uint32_t opcode,
                                             const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations();
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        MemU[address,4] = if t == 15 then PCStoreValue() else R[t];
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    const uint32_t addr_byte_size = GetAddressByteSize();

    switch (encoding) {
    case eEncodingA1:
      // if P == '0' && W == '1' then SEE STRT;
      // if Rn == '1101' && P == '1' && U == '0' && W == '1' && imm12 ==
      // '000000000100' then SEE PUSH;
      // t = UInt(Rt); n = UInt(Rn); imm32 = ZeroExtend(imm12, 32);
      t = Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 11, 0);

      // index = (P == '1'); add = (U == '1'); wback = (P == '0') || (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // if wback && (n == 15 || n == t) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t)))
        return false;

      break;

    default:
      return false;
    }

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    if (add)
      offset_addr = Rn + imm32;
    else
      offset_addr = Rn - imm32;

    // address = if index then offset_addr else R[n];
    addr_t address;
    if (index)
      address = offset_addr;
    else
      address = Rn;

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
    RegisterInfo data_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);
    EmulateInstruction::Context context;
    context.type = eContextRegisterStore;
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, address - Rn);

    // MemU[address,4] = if t == 15 then PCStoreValue() else R[t];
    uint32_t Rt = ReadCoreReg(t, &success);
    if (!success)
      return false;

    if (t == 15) {
      uint32_t pc_value = ReadCoreReg(PC_REG, &success);
      if (!success)
        return false;

      if (!MemUWrite(context, address, pc_value, addr_byte_size))
        return false;
    } else {
      if (!MemUWrite(context, address, Rt, addr_byte_size))
        return false;
    }

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetImmediate(offset_addr);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// A8.6.66 LDRD (immediate)
// Load Register Dual (immediate) calculates an address from a base register
// value and an immediate offset, loads two words from memory, and writes them
// to two registers.  It can use offset, post-indexed, or pre-indexed
// addressing.
bool EmulateInstructionARM::EmulateLDRDImmediate(const uint32_t opcode,
                                                 const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); NullCheckIfThumbEE(n);
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        R[t] = MemA[address,4];
        R[t2] = MemA[address+4,4];
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t t2;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    switch (encoding) {
    case eEncodingT1:
      // if P == '0' && W == '0' then SEE 'Related encodings';
      // if Rn == '1111' then SEE LDRD (literal);
      // t = UInt(Rt); t2 = UInt(Rt2); n = UInt(Rn); imm32 =
      // ZeroExtend(imm8:'00', 32);
      t = Bits32(opcode, 15, 12);
      t2 = Bits32(opcode, 11, 8);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsSet(opcode, 21);

      // if wback && (n == t || n == t2) then UNPREDICTABLE;
      if (wback && ((n == t) || (n == t2)))
        return false;

      // if BadReg(t) || BadReg(t2) || t == t2 then UNPREDICTABLE;
      if (BadReg(t) || BadReg(t2) || (t == t2))
        return false;

      break;

    case eEncodingA1:
      // if Rn == '1111' then SEE LDRD (literal);
      // if Rt<0> == '1' then UNPREDICTABLE;
      // t = UInt(Rt); t2 = t+1; n = UInt(Rn); imm32 = ZeroExtend(imm4H:imm4L,
      // 32);
      t = Bits32(opcode, 15, 12);
      if (BitIsSet(t, 0))
        return false;
      t2 = t + 1;
      n = Bits32(opcode, 19, 16);
      imm32 = (Bits32(opcode, 11, 8) << 4) | Bits32(opcode, 3, 0);

      // index = (P == '1'); add = (U == '1'); wback = (P == '0') || (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // if P == '0' && W == '1' then UNPREDICTABLE;
      if (BitIsClear(opcode, 24) && BitIsSet(opcode, 21))
        return false;

      // if wback && (n == t || n == t2) then UNPREDICTABLE;
      if (wback && ((n == t) || (n == t2)))
        return false;

      // if t2 == 15 then UNPREDICTABLE;
      if (t2 == 15)
        return false;

      break;

    default:
      return false;
    }

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    addr_t offset_addr;
    if (add)
      offset_addr = Rn + imm32;
    else
      offset_addr = Rn - imm32;

    // address = if index then offset_addr else R[n];
    addr_t address;
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // R[t] = MemA[address,4];
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    EmulateInstruction::Context context;
    if (n == 13)
      context.type = eContextPopRegisterOffStack;
    else
      context.type = eContextRegisterLoad;
    context.SetAddress(address);

    const uint32_t addr_byte_size = GetAddressByteSize();
    uint32_t data = MemARead(context, address, addr_byte_size, 0, &success);
    if (!success)
      return false;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t, data))
      return false;

    // R[t2] = MemA[address+4,4];
    context.SetAddress(address + 4);
    data = MemARead(context, address + 4, addr_byte_size, 0, &success);
    if (!success)
      return false;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t2,
                               data))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// A8.6.68 LDRD (register)
// Load Register Dual (register) calculates an address from a base register
// value and a register offset, loads two words from memory, and writes them to
// two registers.  It can use offset, post-indexed or pre-indexed addressing.
bool EmulateInstructionARM::EmulateLDRDRegister(const uint32_t opcode,
                                                const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations();
        offset_addr = if add then (R[n] + R[m]) else (R[n] - R[m]);
        address = if index then offset_addr else R[n];
        R[t] = MemA[address,4];
        R[t2] = MemA[address+4,4];
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t t2;
    uint32_t n;
    uint32_t m;
    bool index;
    bool add;
    bool wback;

    switch (encoding) {
    case eEncodingA1:
      // if Rt<0> == '1' then UNPREDICTABLE;
      // t = UInt(Rt); t2 = t+1; n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      if (BitIsSet(t, 0))
        return false;
      t2 = t + 1;
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1'); add = (U == '1'); wback = (P == '0') || (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // if P == '0' && W == '1' then UNPREDICTABLE;
      if (BitIsClear(opcode, 24) && BitIsSet(opcode, 21))
        return false;

      // if t2 == 15 || m == 15 || m == t || m == t2 then UNPREDICTABLE;
      if ((t2 == 15) || (m == 15) || (m == t) || (m == t2))
        return false;

      // if wback && (n == 15 || n == t || n == t2) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t) || (n == t2)))
        return false;

      // if ArchVersion() < 6 && wback && m == n then UNPREDICTABLE;
      if ((ArchVersion() < 6) && wback && (m == n))
        return false;
      break;

    default:
      return false;
    }

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rm = ReadCoreReg(m, &success);
    if (!success)
      return false;
    RegisterInfo offset_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, offset_reg);

    // offset_addr = if add then (R[n] + R[m]) else (R[n] - R[m]);
    addr_t offset_addr;
    if (add)
      offset_addr = Rn + Rm;
    else
      offset_addr = Rn - Rm;

    // address = if index then offset_addr else R[n];
    addr_t address;
    if (index)
      address = offset_addr;
    else
      address = Rn;

    EmulateInstruction::Context context;
    if (n == 13)
      context.type = eContextPopRegisterOffStack;
    else
      context.type = eContextRegisterLoad;
    context.SetAddress(address);

    // R[t] = MemA[address,4];
    const uint32_t addr_byte_size = GetAddressByteSize();
    uint32_t data = MemARead(context, address, addr_byte_size, 0, &success);
    if (!success)
      return false;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t, data))
      return false;

    // R[t2] = MemA[address+4,4];

    data = MemARead(context, address + 4, addr_byte_size, 0, &success);
    if (!success)
      return false;

    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + t2,
                               data))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// A8.6.200 STRD (immediate)
// Store Register Dual (immediate) calculates an address from a base register
// value and an immediate offset, and stores two words from two registers to
// memory.  It can use offset, post-indexed, or pre-indexed addressing.
bool EmulateInstructionARM::EmulateSTRDImm(const uint32_t opcode,
                                           const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); NullCheckIfThumbEE(n);
        offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
        address = if index then offset_addr else R[n];
        MemA[address,4] = R[t];
        MemA[address+4,4] = R[t2];
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t t2;
    uint32_t n;
    uint32_t imm32;
    bool index;
    bool add;
    bool wback;

    switch (encoding) {
    case eEncodingT1:
      // if P == '0' && W == '0' then SEE 'Related encodings';
      // t = UInt(Rt); t2 = UInt(Rt2); n = UInt(Rn); imm32 =
      // ZeroExtend(imm8:'00', 32);
      t = Bits32(opcode, 15, 12);
      t2 = Bits32(opcode, 11, 8);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // index = (P == '1'); add = (U == '1'); wback = (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsSet(opcode, 21);

      // if wback && (n == t || n == t2) then UNPREDICTABLE;
      if (wback && ((n == t) || (n == t2)))
        return false;

      // if n == 15 || BadReg(t) || BadReg(t2) then UNPREDICTABLE;
      if ((n == 15) || BadReg(t) || BadReg(t2))
        return false;

      break;

    case eEncodingA1:
      // if Rt<0> == '1' then UNPREDICTABLE;
      // t = UInt(Rt); t2 = t+1; n = UInt(Rn); imm32 = ZeroExtend(imm4H:imm4L,
      // 32);
      t = Bits32(opcode, 15, 12);
      if (BitIsSet(t, 0))
        return false;

      t2 = t + 1;
      n = Bits32(opcode, 19, 16);
      imm32 = (Bits32(opcode, 11, 8) << 4) | Bits32(opcode, 3, 0);

      // index = (P == '1'); add = (U == '1'); wback = (P == '0') || (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // if P == '0' && W == '1' then UNPREDICTABLE;
      if (BitIsClear(opcode, 24) && BitIsSet(opcode, 21))
        return false;

      // if wback && (n == 15 || n == t || n == t2) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t) || (n == t2)))
        return false;

      // if t2 == 15 then UNPREDICTABLE;
      if (t2 == 15)
        return false;

      break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // offset_addr = if add then (R[n] + imm32) else (R[n] - imm32);
    addr_t offset_addr;
    if (add)
      offset_addr = Rn + imm32;
    else
      offset_addr = Rn - imm32;

    // address = if index then offset_addr else R[n];
    addr_t address;
    if (index)
      address = offset_addr;
    else
      address = Rn;

    // MemA[address,4] = R[t];
    RegisterInfo data_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);

    uint32_t data = ReadCoreReg(t, &success);
    if (!success)
      return false;

    EmulateInstruction::Context context;
    if (n == 13)
      context.type = eContextPushRegisterOnStack;
    else
      context.type = eContextRegisterStore;
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, address - Rn);

    const uint32_t addr_byte_size = GetAddressByteSize();

    if (!MemAWrite(context, address, data, addr_byte_size))
      return false;

    // MemA[address+4,4] = R[t2];
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t2, data_reg);
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                            (address + 4) - Rn);

    data = ReadCoreReg(t2, &success);
    if (!success)
      return false;

    if (!MemAWrite(context, address + 4, data, addr_byte_size))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      if (n == 13)
        context.type = eContextAdjustStackPointer;
      else
        context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// A8.6.201 STRD (register)
bool EmulateInstructionARM::EmulateSTRDReg(const uint32_t opcode,
                                           const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations();
        offset_addr = if add then (R[n] + R[m]) else (R[n] - R[m]);
        address = if index then offset_addr else R[n];
        MemA[address,4] = R[t];
        MemA[address+4,4] = R[t2];
        if wback then R[n] = offset_addr;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t t;
    uint32_t t2;
    uint32_t n;
    uint32_t m;
    bool index;
    bool add;
    bool wback;

    switch (encoding) {
    case eEncodingA1:
      // if Rt<0> == '1' then UNPREDICTABLE;
      // t = UInt(Rt); t2 = t+1; n = UInt(Rn); m = UInt(Rm);
      t = Bits32(opcode, 15, 12);
      if (BitIsSet(t, 0))
        return false;

      t2 = t + 1;
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // index = (P == '1'); add = (U == '1'); wback = (P == '0') || (W == '1');
      index = BitIsSet(opcode, 24);
      add = BitIsSet(opcode, 23);
      wback = BitIsClear(opcode, 24) || BitIsSet(opcode, 21);

      // if P == '0' && W == '1' then UNPREDICTABLE;
      if (BitIsClear(opcode, 24) && BitIsSet(opcode, 21))
        return false;

      // if t2 == 15 || m == 15 then UNPREDICTABLE;
      if ((t2 == 15) || (m == 15))
        return false;

      // if wback && (n == 15 || n == t || n == t2) then UNPREDICTABLE;
      if (wback && ((n == 15) || (n == t) || (n == t2)))
        return false;

      // if ArchVersion() < 6 && wback && m == n then UNPREDICTABLE;
      if ((ArchVersion() < 6) && wback && (m == n))
        return false;

      break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);
    RegisterInfo offset_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + m, offset_reg);
    RegisterInfo data_reg;

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    uint32_t Rm = ReadCoreReg(m, &success);
    if (!success)
      return false;

    // offset_addr = if add then (R[n] + R[m]) else (R[n] - R[m]);
    addr_t offset_addr;
    if (add)
      offset_addr = Rn + Rm;
    else
      offset_addr = Rn - Rm;

    // address = if index then offset_addr else R[n];
    addr_t address;
    if (index)
      address = offset_addr;
    else
      address = Rn;
    // MemA[address,4] = R[t];
    uint32_t Rt = ReadCoreReg(t, &success);
    if (!success)
      return false;

    EmulateInstruction::Context context;
    if (t == 13)
      context.type = eContextPushRegisterOnStack;
    else
      context.type = eContextRegisterStore;

    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t, data_reg);
    context.SetRegisterToRegisterPlusIndirectOffset(base_reg, offset_reg,
                                                    data_reg);

    const uint32_t addr_byte_size = GetAddressByteSize();

    if (!MemAWrite(context, address, Rt, addr_byte_size))
      return false;

    // MemA[address+4,4] = R[t2];
    uint32_t Rt2 = ReadCoreReg(t2, &success);
    if (!success)
      return false;

    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + t2, data_reg);

    context.SetRegisterToRegisterPlusIndirectOffset(base_reg, offset_reg,
                                                    data_reg);

    if (!MemAWrite(context, address + 4, Rt2, addr_byte_size))
      return false;

    // if wback then R[n] = offset_addr;
    if (wback) {
      context.type = eContextAdjustBaseRegister;
      context.SetAddress(offset_addr);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 offset_addr))
        return false;
    }
  }
  return true;
}

// A8.6.319 VLDM
// Vector Load Multiple loads multiple extension registers from consecutive
// memory locations using an address from an ARM core register.
bool EmulateInstructionARM::EmulateVLDM(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(n);
        address = if add then R[n] else R[n]-imm32;
        if wback then R[n] = if add then R[n]+imm32 else R[n]-imm32;
        for r = 0 to regs-1
            if single_regs then
                S[d+r] = MemA[address,4]; address = address+4;
            else
                word1 = MemA[address,4]; word2 = MemA[address+4,4]; address = address+8;
                // Combine the word-aligned words in the correct order for
                // current endianness.
                D[d+r] = if BigEndian() then word1:word2 else word2:word1;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    bool single_regs;
    bool add;
    bool wback;
    uint32_t d;
    uint32_t n;
    uint32_t imm32;
    uint32_t regs;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1:
      // if P == '0' && U == '0' && W == '0' then SEE 'Related encodings';
      // if P == '0' && U == '1' && W == '1' && Rn == '1101' then SEE VPOP;
      // if P == '1' && W == '0' then SEE VLDR;
      // if P == U && W == '1' then UNDEFINED;
      if ((Bit32(opcode, 24) == Bit32(opcode, 23)) && BitIsSet(opcode, 21))
        return false;

      // // Remaining combinations are PUW = 010 (IA without !), 011 (IA with
      // !), 101 (DB with !)
      // single_regs = FALSE; add = (U == '1'); wback = (W == '1');
      single_regs = false;
      add = BitIsSet(opcode, 23);
      wback = BitIsSet(opcode, 21);

      // d = UInt(D:Vd); n = UInt(Rn); imm32 = ZeroExtend(imm8:'00', 32);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // regs = UInt(imm8) DIV 2; // If UInt(imm8) is odd, see 'FLDMX'.
      regs = Bits32(opcode, 7, 0) / 2;

      // if n == 15 && (wback || CurrentInstrSet() != InstrSet_ARM) then
      // UNPREDICTABLE;
      if (n == 15 && (wback || CurrentInstrSet() != eModeARM))
        return false;

      // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
      if ((regs == 0) || (regs > 16) || ((d + regs) > 32))
        return false;

      break;

    case eEncodingT2:
    case eEncodingA2:
      // if P == '0' && U == '0' && W == '0' then SEE 'Related encodings';
      // if P == '0' && U == '1' && W == '1' && Rn == '1101' then SEE VPOP;
      // if P == '1' && W == '0' then SEE VLDR;
      // if P == U && W == '1' then UNDEFINED;
      if ((Bit32(opcode, 24) == Bit32(opcode, 23)) && BitIsSet(opcode, 21))
        return false;

      // // Remaining combinations are PUW = 010 (IA without !), 011 (IA with
      // !), 101 (DB with !) single_regs = TRUE; add = (U == '1'); wback = (W
      // == '1'); d =
      // UInt(Vd:D); n = UInt(Rn);
      single_regs = true;
      add = BitIsSet(opcode, 23);
      wback = BitIsSet(opcode, 21);
      d = (Bits32(opcode, 15, 12) << 1) | Bit32(opcode, 22);
      n = Bits32(opcode, 19, 16);

      // imm32 = ZeroExtend(imm8:'00', 32); regs = UInt(imm8);
      imm32 = Bits32(opcode, 7, 0) << 2;
      regs = Bits32(opcode, 7, 0);

      // if n == 15 && (wback || CurrentInstrSet() != InstrSet_ARM) then
      // UNPREDICTABLE;
      if ((n == 15) && (wback || (CurrentInstrSet() != eModeARM)))
        return false;

      // if regs == 0 || (d+regs) > 32 then UNPREDICTABLE;
      if ((regs == 0) || ((d + regs) > 32))
        return false;
      break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // address = if add then R[n] else R[n]-imm32;
    addr_t address;
    if (add)
      address = Rn;
    else
      address = Rn - imm32;

    // if wback then R[n] = if add then R[n]+imm32 else R[n]-imm32;
    EmulateInstruction::Context context;

    if (wback) {
      uint32_t value;
      if (add)
        value = Rn + imm32;
      else
        value = Rn - imm32;

      context.type = eContextAdjustBaseRegister;
      context.SetImmediateSigned(value - Rn);
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 value))
        return false;
    }

    const uint32_t addr_byte_size = GetAddressByteSize();
    uint32_t start_reg = single_regs ? dwarf_s0 : dwarf_d0;

    context.type = eContextRegisterLoad;

    // for r = 0 to regs-1
    for (uint32_t r = 0; r < regs; ++r) {
      if (single_regs) {
        // S[d+r] = MemA[address,4]; address = address+4;
        context.SetRegisterPlusOffset(base_reg, address - Rn);

        uint32_t data = MemARead(context, address, addr_byte_size, 0, &success);
        if (!success)
          return false;

        if (!WriteRegisterUnsigned(context, eRegisterKindDWARF,
                                   start_reg + d + r, data))
          return false;

        address = address + 4;
      } else {
        // word1 = MemA[address,4]; word2 = MemA[address+4,4]; address =
        // address+8;
        context.SetRegisterPlusOffset(base_reg, address - Rn);
        uint32_t word1 =
            MemARead(context, address, addr_byte_size, 0, &success);
        if (!success)
          return false;

        context.SetRegisterPlusOffset(base_reg, (address + 4) - Rn);
        uint32_t word2 =
            MemARead(context, address + 4, addr_byte_size, 0, &success);
        if (!success)
          return false;

        address = address + 8;
        // // Combine the word-aligned words in the correct order for current
        // endianness.
        // D[d+r] = if BigEndian() then word1:word2 else word2:word1;
        uint64_t data;
        if (GetByteOrder() == eByteOrderBig) {
          data = word1;
          data = (data << 32) | word2;
        } else {
          data = word2;
          data = (data << 32) | word1;
        }

        if (!WriteRegisterUnsigned(context, eRegisterKindDWARF,
                                   start_reg + d + r, data))
          return false;
      }
    }
  }
  return true;
}

// A8.6.399 VSTM
// Vector Store Multiple stores multiple extension registers to consecutive
// memory locations using an address from an
// ARM core register.
bool EmulateInstructionARM::EmulateVSTM(const uint32_t opcode,
                                        const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(n);
        address = if add then R[n] else R[n]-imm32;
        if wback then R[n] = if add then R[n]+imm32 else R[n]-imm32;
        for r = 0 to regs-1
            if single_regs then
                MemA[address,4] = S[d+r]; address = address+4;
            else
                // Store as two word-aligned words in the correct order for
                // current endianness.
                MemA[address,4] = if BigEndian() then D[d+r]<63:32> else D[d+r]<31:0>;
                MemA[address+4,4] = if BigEndian() then D[d+r]<31:0> else D[d+r]<63:32>;
                address = address+8;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    bool single_regs;
    bool add;
    bool wback;
    uint32_t d;
    uint32_t n;
    uint32_t imm32;
    uint32_t regs;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1:
      // if P == '0' && U == '0' && W == '0' then SEE 'Related encodings';
      // if P == '1' && U == '0' && W == '1' && Rn == '1101' then SEE VPUSH;
      // if P == '1' && W == '0' then SEE VSTR;
      // if P == U && W == '1' then UNDEFINED;
      if ((Bit32(opcode, 24) == Bit32(opcode, 23)) && BitIsSet(opcode, 21))
        return false;

      // // Remaining combinations are PUW = 010 (IA without !), 011 (IA with
      // !), 101 (DB with !)
      // single_regs = FALSE; add = (U == '1'); wback = (W == '1');
      single_regs = false;
      add = BitIsSet(opcode, 23);
      wback = BitIsSet(opcode, 21);

      // d = UInt(D:Vd); n = UInt(Rn); imm32 = ZeroExtend(imm8:'00', 32);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // regs = UInt(imm8) DIV 2; // If UInt(imm8) is odd, see 'FSTMX'.
      regs = Bits32(opcode, 7, 0) / 2;

      // if n == 15 && (wback || CurrentInstrSet() != InstrSet_ARM) then
      // UNPREDICTABLE;
      if ((n == 15) && (wback || (CurrentInstrSet() != eModeARM)))
        return false;

      // if regs == 0 || regs > 16 || (d+regs) > 32 then UNPREDICTABLE;
      if ((regs == 0) || (regs > 16) || ((d + regs) > 32))
        return false;

      break;

    case eEncodingT2:
    case eEncodingA2:
      // if P == '0' && U == '0' && W == '0' then SEE 'Related encodings';
      // if P == '1' && U == '0' && W == '1' && Rn == '1101' then SEE VPUSH;
      // if P == '1' && W == '0' then SEE VSTR;
      // if P == U && W == '1' then UNDEFINED;
      if ((Bit32(opcode, 24) == Bit32(opcode, 23)) && BitIsSet(opcode, 21))
        return false;

      // // Remaining combinations are PUW = 010 (IA without !), 011 (IA with
      // !), 101 (DB with !) single_regs = TRUE; add = (U == '1'); wback = (W
      // == '1'); d =
      // UInt(Vd:D); n = UInt(Rn);
      single_regs = true;
      add = BitIsSet(opcode, 23);
      wback = BitIsSet(opcode, 21);
      d = (Bits32(opcode, 15, 12) << 1) | Bit32(opcode, 22);
      n = Bits32(opcode, 19, 16);

      // imm32 = ZeroExtend(imm8:'00', 32); regs = UInt(imm8);
      imm32 = Bits32(opcode, 7, 0) << 2;
      regs = Bits32(opcode, 7, 0);

      // if n == 15 && (wback || CurrentInstrSet() != InstrSet_ARM) then
      // UNPREDICTABLE;
      if ((n == 15) && (wback || (CurrentInstrSet() != eModeARM)))
        return false;

      // if regs == 0 || (d+regs) > 32 then UNPREDICTABLE;
      if ((regs == 0) || ((d + regs) > 32))
        return false;

      break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // address = if add then R[n] else R[n]-imm32;
    addr_t address;
    if (add)
      address = Rn;
    else
      address = Rn - imm32;

    EmulateInstruction::Context context;
    // if wback then R[n] = if add then R[n]+imm32 else R[n]-imm32;
    if (wback) {
      uint32_t value;
      if (add)
        value = Rn + imm32;
      else
        value = Rn - imm32;

      context.type = eContextAdjustBaseRegister;
      context.SetRegisterPlusOffset(base_reg, value - Rn);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 value))
        return false;
    }

    const uint32_t addr_byte_size = GetAddressByteSize();
    uint32_t start_reg = single_regs ? dwarf_s0 : dwarf_d0;

    context.type = eContextRegisterStore;
    // for r = 0 to regs-1
    for (uint32_t r = 0; r < regs; ++r) {

      if (single_regs) {
        // MemA[address,4] = S[d+r]; address = address+4;
        uint32_t data = ReadRegisterUnsigned(eRegisterKindDWARF,
                                             start_reg + d + r, 0, &success);
        if (!success)
          return false;

        RegisterInfo data_reg;
        GetRegisterInfo(eRegisterKindDWARF, start_reg + d + r, data_reg);
        context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                address - Rn);
        if (!MemAWrite(context, address, data, addr_byte_size))
          return false;

        address = address + 4;
      } else {
        // // Store as two word-aligned words in the correct order for current
        // endianness. MemA[address,4] = if BigEndian() then D[d+r]<63:32> else
        // D[d+r]<31:0>;
        // MemA[address+4,4] = if BigEndian() then D[d+r]<31:0> else
        // D[d+r]<63:32>;
        uint64_t data = ReadRegisterUnsigned(eRegisterKindDWARF,
                                             start_reg + d + r, 0, &success);
        if (!success)
          return false;

        RegisterInfo data_reg;
        GetRegisterInfo(eRegisterKindDWARF, start_reg + d + r, data_reg);

        if (GetByteOrder() == eByteOrderBig) {
          context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                  address - Rn);
          if (!MemAWrite(context, address, Bits64(data, 63, 32),
                         addr_byte_size))
            return false;

          context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                  (address + 4) - Rn);
          if (!MemAWrite(context, address + 4, Bits64(data, 31, 0),
                         addr_byte_size))
            return false;
        } else {
          context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                  address - Rn);
          if (!MemAWrite(context, address, Bits64(data, 31, 0), addr_byte_size))
            return false;

          context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                  (address + 4) - Rn);
          if (!MemAWrite(context, address + 4, Bits64(data, 63, 32),
                         addr_byte_size))
            return false;
        }
        // address = address+8;
        address = address + 8;
      }
    }
  }
  return true;
}

// A8.6.320
// This instruction loads a single extension register from memory, using an
// address from an ARM core register, with an optional offset.
bool EmulateInstructionARM::EmulateVLDR(const uint32_t opcode,
                                        ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(n);
        base = if n == 15 then Align(PC,4) else R[n];
        address = if add then (base + imm32) else (base - imm32);
        if single_reg then
            S[d] = MemA[address,4];
        else
            word1 = MemA[address,4]; word2 = MemA[address+4,4];
            // Combine the word-aligned words in the correct order for current
            // endianness.
            D[d] = if BigEndian() then word1:word2 else word2:word1;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    bool single_reg;
    bool add;
    uint32_t imm32;
    uint32_t d;
    uint32_t n;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1:
      // single_reg = FALSE; add = (U == '1'); imm32 = ZeroExtend(imm8:'00',
      // 32);
      single_reg = false;
      add = BitIsSet(opcode, 23);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // d = UInt(D:Vd); n = UInt(Rn);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);

      break;

    case eEncodingT2:
    case eEncodingA2:
      // single_reg = TRUE; add = (U == '1'); imm32 = ZeroExtend(imm8:'00', 32);
      single_reg = true;
      add = BitIsSet(opcode, 23);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // d = UInt(Vd:D); n = UInt(Rn);
      d = (Bits32(opcode, 15, 12) << 1) | Bit32(opcode, 22);
      n = Bits32(opcode, 19, 16);

      break;

    default:
      return false;
    }
    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // base = if n == 15 then Align(PC,4) else R[n];
    uint32_t base;
    if (n == 15)
      base = AlignPC(Rn);
    else
      base = Rn;

    // address = if add then (base + imm32) else (base - imm32);
    addr_t address;
    if (add)
      address = base + imm32;
    else
      address = base - imm32;

    const uint32_t addr_byte_size = GetAddressByteSize();
    uint32_t start_reg = single_reg ? dwarf_s0 : dwarf_d0;

    EmulateInstruction::Context context;
    context.type = eContextRegisterLoad;
    context.SetRegisterPlusOffset(base_reg, address - base);

    if (single_reg) {
      // S[d] = MemA[address,4];
      uint32_t data = MemARead(context, address, addr_byte_size, 0, &success);
      if (!success)
        return false;

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, start_reg + d,
                                 data))
        return false;
    } else {
      // word1 = MemA[address,4]; word2 = MemA[address+4,4];
      uint32_t word1 = MemARead(context, address, addr_byte_size, 0, &success);
      if (!success)
        return false;

      context.SetRegisterPlusOffset(base_reg, (address + 4) - base);
      uint32_t word2 =
          MemARead(context, address + 4, addr_byte_size, 0, &success);
      if (!success)
        return false;
      // // Combine the word-aligned words in the correct order for current
      // endianness.
      // D[d] = if BigEndian() then word1:word2 else word2:word1;
      uint64_t data64;
      if (GetByteOrder() == eByteOrderBig) {
        data64 = word1;
        data64 = (data64 << 32) | word2;
      } else {
        data64 = word2;
        data64 = (data64 << 32) | word1;
      }

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, start_reg + d,
                                 data64))
        return false;
    }
  }
  return true;
}

// A8.6.400 VSTR
// This instruction stores a signle extension register to memory, using an
// address from an ARM core register, with an optional offset.
bool EmulateInstructionARM::EmulateVSTR(const uint32_t opcode,
                                        ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckVFPEnabled(TRUE); NullCheckIfThumbEE(n);
        address = if add then (R[n] + imm32) else (R[n] - imm32);
        if single_reg then
            MemA[address,4] = S[d];
        else
            // Store as two word-aligned words in the correct order for current
            // endianness.
            MemA[address,4] = if BigEndian() then D[d]<63:32> else D[d]<31:0>;
            MemA[address+4,4] = if BigEndian() then D[d]<31:0> else D[d]<63:32>;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    bool single_reg;
    bool add;
    uint32_t imm32;
    uint32_t d;
    uint32_t n;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1:
      // single_reg = FALSE; add = (U == '1'); imm32 = ZeroExtend(imm8:'00',
      // 32);
      single_reg = false;
      add = BitIsSet(opcode, 23);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // d = UInt(D:Vd); n = UInt(Rn);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);

      // if n == 15 && CurrentInstrSet() != InstrSet_ARM then UNPREDICTABLE;
      if ((n == 15) && (CurrentInstrSet() != eModeARM))
        return false;

      break;

    case eEncodingT2:
    case eEncodingA2:
      // single_reg = TRUE; add = (U == '1'); imm32 = ZeroExtend(imm8:'00', 32);
      single_reg = true;
      add = BitIsSet(opcode, 23);
      imm32 = Bits32(opcode, 7, 0) << 2;

      // d = UInt(Vd:D); n = UInt(Rn);
      d = (Bits32(opcode, 15, 12) << 1) | Bit32(opcode, 22);
      n = Bits32(opcode, 19, 16);

      // if n == 15 && CurrentInstrSet() != InstrSet_ARM then UNPREDICTABLE;
      if ((n == 15) && (CurrentInstrSet() != eModeARM))
        return false;

      break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // address = if add then (R[n] + imm32) else (R[n] - imm32);
    addr_t address;
    if (add)
      address = Rn + imm32;
    else
      address = Rn - imm32;

    const uint32_t addr_byte_size = GetAddressByteSize();
    uint32_t start_reg = single_reg ? dwarf_s0 : dwarf_d0;

    RegisterInfo data_reg;
    GetRegisterInfo(eRegisterKindDWARF, start_reg + d, data_reg);
    EmulateInstruction::Context context;
    context.type = eContextRegisterStore;
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, address - Rn);

    if (single_reg) {
      // MemA[address,4] = S[d];
      uint32_t data =
          ReadRegisterUnsigned(eRegisterKindDWARF, start_reg + d, 0, &success);
      if (!success)
        return false;

      if (!MemAWrite(context, address, data, addr_byte_size))
        return false;
    } else {
      // // Store as two word-aligned words in the correct order for current
      // endianness.
      // MemA[address,4] = if BigEndian() then D[d]<63:32> else D[d]<31:0>;
      // MemA[address+4,4] = if BigEndian() then D[d]<31:0> else D[d]<63:32>;
      uint64_t data =
          ReadRegisterUnsigned(eRegisterKindDWARF, start_reg + d, 0, &success);
      if (!success)
        return false;

      if (GetByteOrder() == eByteOrderBig) {
        if (!MemAWrite(context, address, Bits64(data, 63, 32), addr_byte_size))
          return false;

        context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                (address + 4) - Rn);
        if (!MemAWrite(context, address + 4, Bits64(data, 31, 0),
                       addr_byte_size))
          return false;
      } else {
        if (!MemAWrite(context, address, Bits64(data, 31, 0), addr_byte_size))
          return false;

        context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                (address + 4) - Rn);
        if (!MemAWrite(context, address + 4, Bits64(data, 63, 32),
                       addr_byte_size))
          return false;
      }
    }
  }
  return true;
}

// A8.6.307 VLDI1 (multiple single elements) This instruction loads elements
// from memory into one, two, three or four registers, without de-interleaving.
// Every element of each register is loaded.
bool EmulateInstructionARM::EmulateVLD1Multiple(const uint32_t opcode,
                                                ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckAdvSIMDEnabled(); NullCheckIfThumbEE(n);
        address = R[n]; if (address MOD alignment) != 0 then GenerateAlignmentException();
        if wback then R[n] = R[n] + (if register_index then R[m] else 8*regs);
        for r = 0 to regs-1
            for e = 0 to elements-1
                Elem[D[d+r],e,esize] = MemU[address,ebytes];
                address = address + ebytes;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t regs;
    uint32_t alignment;
    uint32_t ebytes;
    uint32_t esize;
    uint32_t elements;
    uint32_t d;
    uint32_t n;
    uint32_t m;
    bool wback;
    bool register_index;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1: {
      // case type of
      // when '0111'
      // regs = 1; if align<1> == '1' then UNDEFINED;
      // when '1010'
      // regs = 2; if align == '11' then UNDEFINED;
      // when '0110'
      // regs = 3; if align<1> == '1' then UNDEFINED;
      // when '0010'
      // regs = 4;
      // otherwise
      // SEE 'Related encodings';
      uint32_t type = Bits32(opcode, 11, 8);
      uint32_t align = Bits32(opcode, 5, 4);
      if (type == 7) // '0111'
      {
        regs = 1;
        if (BitIsSet(align, 1))
          return false;
      } else if (type == 10) // '1010'
      {
        regs = 2;
        if (align == 3)
          return false;

      } else if (type == 6) // '0110'
      {
        regs = 3;
        if (BitIsSet(align, 1))
          return false;
      } else if (type == 2) // '0010'
      {
        regs = 4;
      } else
        return false;

      // alignment = if align == '00' then 1 else 4 << UInt(align);
      if (align == 0)
        alignment = 1;
      else
        alignment = 4 << align;

      // ebytes = 1 << UInt(size); esize = 8 * ebytes; elements = 8 DIV ebytes;
      ebytes = 1 << Bits32(opcode, 7, 6);
      esize = 8 * ebytes;
      elements = 8 / ebytes;

      // d = UInt(D:Vd); n = UInt(Rn); m = UInt(Rm);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 15);
      m = Bits32(opcode, 3, 0);

      // wback = (m != 15); register_index = (m != 15 && m != 13);
      wback = (m != 15);
      register_index = ((m != 15) && (m != 13));

      // if d+regs > 32 then UNPREDICTABLE;
      if ((d + regs) > 32)
        return false;
    } break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // address = R[n]; if (address MOD alignment) != 0 then
    // GenerateAlignmentException();
    addr_t address = Rn;
    if ((address % alignment) != 0)
      return false;

    EmulateInstruction::Context context;
    // if wback then R[n] = R[n] + (if register_index then R[m] else 8*regs);
    if (wback) {
      uint32_t Rm = ReadCoreReg(m, &success);
      if (!success)
        return false;

      uint32_t offset;
      if (register_index)
        offset = Rm;
      else
        offset = 8 * regs;

      uint32_t value = Rn + offset;
      context.type = eContextAdjustBaseRegister;
      context.SetRegisterPlusOffset(base_reg, offset);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 value))
        return false;
    }

    // for r = 0 to regs-1
    for (uint32_t r = 0; r < regs; ++r) {
      // for e = 0 to elements-1
      uint64_t assembled_data = 0;
      for (uint32_t e = 0; e < elements; ++e) {
        // Elem[D[d+r],e,esize] = MemU[address,ebytes];
        context.type = eContextRegisterLoad;
        context.SetRegisterPlusOffset(base_reg, address - Rn);
        uint64_t data = MemURead(context, address, ebytes, 0, &success);
        if (!success)
          return false;

        assembled_data =
            (data << (e * esize)) |
            assembled_data; // New data goes to the left of existing data

        // address = address + ebytes;
        address = address + ebytes;
      }
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_d0 + d + r,
                                 assembled_data))
        return false;
    }
  }
  return true;
}

// A8.6.308 VLD1 (single element to one lane)
//
bool EmulateInstructionARM::EmulateVLD1Single(const uint32_t opcode,
                                              const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckAdvSIMDEnabled(); NullCheckIfThumbEE(n);
        address = R[n]; if (address MOD alignment) != 0 then GenerateAlignmentException();
        if wback then R[n] = R[n] + (if register_index then R[m] else ebytes);
        Elem[D[d],index,esize] = MemU[address,ebytes];
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t ebytes;
    uint32_t esize;
    uint32_t index;
    uint32_t alignment;
    uint32_t d;
    uint32_t n;
    uint32_t m;
    bool wback;
    bool register_index;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1: {
      uint32_t size = Bits32(opcode, 11, 10);
      uint32_t index_align = Bits32(opcode, 7, 4);
      // if size == '11' then SEE VLD1 (single element to all lanes);
      if (size == 3)
        return EmulateVLD1SingleAll(opcode, encoding);
      // case size of
      if (size == 0) // when '00'
      {
        // if index_align<0> != '0' then UNDEFINED;
        if (BitIsClear(index_align, 0))
          return false;

        // ebytes = 1; esize = 8; index = UInt(index_align<3:1>); alignment = 1;
        ebytes = 1;
        esize = 8;
        index = Bits32(index_align, 3, 1);
        alignment = 1;
      } else if (size == 1) // when '01'
      {
        // if index_align<1> != '0' then UNDEFINED;
        if (BitIsClear(index_align, 1))
          return false;

        // ebytes = 2; esize = 16; index = UInt(index_align<3:2>);
        ebytes = 2;
        esize = 16;
        index = Bits32(index_align, 3, 2);

        // alignment = if index_align<0> == '0' then 1 else 2;
        if (BitIsClear(index_align, 0))
          alignment = 1;
        else
          alignment = 2;
      } else if (size == 2) // when '10'
      {
        // if index_align<2> != '0' then UNDEFINED;
        if (BitIsClear(index_align, 2))
          return false;

        // if index_align<1:0> != '00' && index_align<1:0> != '11' then
        // UNDEFINED;
        if ((Bits32(index_align, 1, 0) != 0) &&
            (Bits32(index_align, 1, 0) != 3))
          return false;

        // ebytes = 4; esize = 32; index = UInt(index_align<3>);
        ebytes = 4;
        esize = 32;
        index = Bit32(index_align, 3);

        // alignment = if index_align<1:0> == '00' then 1 else 4;
        if (Bits32(index_align, 1, 0) == 0)
          alignment = 1;
        else
          alignment = 4;
      } else {
        return false;
      }
      // d = UInt(D:Vd); n = UInt(Rn); m = UInt(Rm);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // wback = (m != 15); register_index = (m != 15 && m != 13); if n == 15
      // then UNPREDICTABLE;
      wback = (m != 15);
      register_index = ((m != 15) && (m != 13));

      if (n == 15)
        return false;

    } break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // address = R[n]; if (address MOD alignment) != 0 then
    // GenerateAlignmentException();
    addr_t address = Rn;
    if ((address % alignment) != 0)
      return false;

    EmulateInstruction::Context context;
    // if wback then R[n] = R[n] + (if register_index then R[m] else ebytes);
    if (wback) {
      uint32_t Rm = ReadCoreReg(m, &success);
      if (!success)
        return false;

      uint32_t offset;
      if (register_index)
        offset = Rm;
      else
        offset = ebytes;

      uint32_t value = Rn + offset;

      context.type = eContextAdjustBaseRegister;
      context.SetRegisterPlusOffset(base_reg, offset);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 value))
        return false;
    }

    // Elem[D[d],index,esize] = MemU[address,ebytes];
    uint32_t element = MemURead(context, address, esize, 0, &success);
    if (!success)
      return false;

    element = element << (index * esize);

    uint64_t reg_data =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_d0 + d, 0, &success);
    if (!success)
      return false;

    uint64_t all_ones = -1;
    uint64_t mask = all_ones
                    << ((index + 1) * esize); // mask is all 1's to left of
                                              // where 'element' goes, & all 0's
    // at element & to the right of element.
    if (index > 0)
      mask = mask | Bits64(all_ones, (index * esize) - 1,
                           0); // add 1's to the right of where 'element' goes.
    // now mask should be 0's where element goes & 1's everywhere else.

    uint64_t masked_reg =
        reg_data & mask; // Take original reg value & zero out 'element' bits
    reg_data =
        masked_reg & element; // Put 'element' into those bits in reg_data.

    context.type = eContextRegisterLoad;
    if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + d,
                               reg_data))
      return false;
  }
  return true;
}

// A8.6.391 VST1 (multiple single elements) Vector Store (multiple single
// elements) stores elements to memory from one, two, three, or four registers,
// without interleaving.  Every element of each register is stored.
bool EmulateInstructionARM::EmulateVST1Multiple(const uint32_t opcode,
                                                ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckAdvSIMDEnabled(); NullCheckIfThumbEE(n);
        address = R[n]; if (address MOD alignment) != 0 then GenerateAlignmentException();
        if wback then R[n] = R[n] + (if register_index then R[m] else 8*regs);
        for r = 0 to regs-1
            for e = 0 to elements-1
                MemU[address,ebytes] = Elem[D[d+r],e,esize];
                address = address + ebytes;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t regs;
    uint32_t alignment;
    uint32_t ebytes;
    uint32_t esize;
    uint32_t elements;
    uint32_t d;
    uint32_t n;
    uint32_t m;
    bool wback;
    bool register_index;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1: {
      uint32_t type = Bits32(opcode, 11, 8);
      uint32_t align = Bits32(opcode, 5, 4);

      // case type of
      if (type == 7) // when '0111'
      {
        // regs = 1; if align<1> == '1' then UNDEFINED;
        regs = 1;
        if (BitIsSet(align, 1))
          return false;
      } else if (type == 10) // when '1010'
      {
        // regs = 2; if align == '11' then UNDEFINED;
        regs = 2;
        if (align == 3)
          return false;
      } else if (type == 6) // when '0110'
      {
        // regs = 3; if align<1> == '1' then UNDEFINED;
        regs = 3;
        if (BitIsSet(align, 1))
          return false;
      } else if (type == 2) // when '0010'
        // regs = 4;
        regs = 4;
      else // otherwise
        // SEE 'Related encodings';
        return false;

      // alignment = if align == '00' then 1 else 4 << UInt(align);
      if (align == 0)
        alignment = 1;
      else
        alignment = 4 << align;

      // ebytes = 1 << UInt(size); esize = 8 * ebytes; elements = 8 DIV ebytes;
      ebytes = 1 << Bits32(opcode, 7, 6);
      esize = 8 * ebytes;
      elements = 8 / ebytes;

      // d = UInt(D:Vd); n = UInt(Rn); m = UInt(Rm);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // wback = (m != 15); register_index = (m != 15 && m != 13);
      wback = (m != 15);
      register_index = ((m != 15) && (m != 13));

      // if d+regs > 32 then UNPREDICTABLE; if n == 15 then UNPREDICTABLE;
      if ((d + regs) > 32)
        return false;

      if (n == 15)
        return false;

    } break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // address = R[n]; if (address MOD alignment) != 0 then
    // GenerateAlignmentException();
    addr_t address = Rn;
    if ((address % alignment) != 0)
      return false;

    EmulateInstruction::Context context;
    // if wback then R[n] = R[n] + (if register_index then R[m] else 8*regs);
    if (wback) {
      uint32_t Rm = ReadCoreReg(m, &success);
      if (!success)
        return false;

      uint32_t offset;
      if (register_index)
        offset = Rm;
      else
        offset = 8 * regs;

      context.type = eContextAdjustBaseRegister;
      context.SetRegisterPlusOffset(base_reg, offset);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 Rn + offset))
        return false;
    }

    RegisterInfo data_reg;
    context.type = eContextRegisterStore;
    // for r = 0 to regs-1
    for (uint32_t r = 0; r < regs; ++r) {
      GetRegisterInfo(eRegisterKindDWARF, dwarf_d0 + d + r, data_reg);
      uint64_t register_data = ReadRegisterUnsigned(
          eRegisterKindDWARF, dwarf_d0 + d + r, 0, &success);
      if (!success)
        return false;

      // for e = 0 to elements-1
      for (uint32_t e = 0; e < elements; ++e) {
        // MemU[address,ebytes] = Elem[D[d+r],e,esize];
        uint64_t word = Bits64(register_data, ((e + 1) * esize) - 1, e * esize);

        context.SetRegisterToRegisterPlusOffset(data_reg, base_reg,
                                                address - Rn);
        if (!MemUWrite(context, address, word, ebytes))
          return false;

        // address = address + ebytes;
        address = address + ebytes;
      }
    }
  }
  return true;
}

// A8.6.392 VST1 (single element from one lane) This instruction stores one
// element to memory from one element of a register.
bool EmulateInstructionARM::EmulateVST1Single(const uint32_t opcode,
                                              ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckAdvSIMDEnabled(); NullCheckIfThumbEE(n);
        address = R[n]; if (address MOD alignment) != 0 then GenerateAlignmentException();
        if wback then R[n] = R[n] + (if register_index then R[m] else ebytes);
        MemU[address,ebytes] = Elem[D[d],index,esize];
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t ebytes;
    uint32_t esize;
    uint32_t index;
    uint32_t alignment;
    uint32_t d;
    uint32_t n;
    uint32_t m;
    bool wback;
    bool register_index;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1: {
      uint32_t size = Bits32(opcode, 11, 10);
      uint32_t index_align = Bits32(opcode, 7, 4);

      // if size == '11' then UNDEFINED;
      if (size == 3)
        return false;

      // case size of
      if (size == 0) // when '00'
      {
        // if index_align<0> != '0' then UNDEFINED;
        if (BitIsClear(index_align, 0))
          return false;
        // ebytes = 1; esize = 8; index = UInt(index_align<3:1>); alignment = 1;
        ebytes = 1;
        esize = 8;
        index = Bits32(index_align, 3, 1);
        alignment = 1;
      } else if (size == 1) // when '01'
      {
        // if index_align<1> != '0' then UNDEFINED;
        if (BitIsClear(index_align, 1))
          return false;

        // ebytes = 2; esize = 16; index = UInt(index_align<3:2>);
        ebytes = 2;
        esize = 16;
        index = Bits32(index_align, 3, 2);

        // alignment = if index_align<0> == '0' then 1 else 2;
        if (BitIsClear(index_align, 0))
          alignment = 1;
        else
          alignment = 2;
      } else if (size == 2) // when '10'
      {
        // if index_align<2> != '0' then UNDEFINED;
        if (BitIsClear(index_align, 2))
          return false;

        // if index_align<1:0> != '00' && index_align<1:0> != '11' then
        // UNDEFINED;
        if ((Bits32(index_align, 1, 0) != 0) &&
            (Bits32(index_align, 1, 0) != 3))
          return false;

        // ebytes = 4; esize = 32; index = UInt(index_align<3>);
        ebytes = 4;
        esize = 32;
        index = Bit32(index_align, 3);

        // alignment = if index_align<1:0> == '00' then 1 else 4;
        if (Bits32(index_align, 1, 0) == 0)
          alignment = 1;
        else
          alignment = 4;
      } else {
        return false;
      }
      // d = UInt(D:Vd); n = UInt(Rn); m = UInt(Rm);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // wback = (m != 15); register_index = (m != 15 && m != 13);  if n == 15
      // then UNPREDICTABLE;
      wback = (m != 15);
      register_index = ((m != 15) && (m != 13));

      if (n == 15)
        return false;
    } break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // address = R[n]; if (address MOD alignment) != 0 then
    // GenerateAlignmentException();
    addr_t address = Rn;
    if ((address % alignment) != 0)
      return false;

    EmulateInstruction::Context context;
    // if wback then R[n] = R[n] + (if register_index then R[m] else ebytes);
    if (wback) {
      uint32_t Rm = ReadCoreReg(m, &success);
      if (!success)
        return false;

      uint32_t offset;
      if (register_index)
        offset = Rm;
      else
        offset = ebytes;

      context.type = eContextAdjustBaseRegister;
      context.SetRegisterPlusOffset(base_reg, offset);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 Rn + offset))
        return false;
    }

    // MemU[address,ebytes] = Elem[D[d],index,esize];
    uint64_t register_data =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_d0 + d, 0, &success);
    if (!success)
      return false;

    uint64_t word =
        Bits64(register_data, ((index + 1) * esize) - 1, index * esize);

    RegisterInfo data_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_d0 + d, data_reg);
    context.type = eContextRegisterStore;
    context.SetRegisterToRegisterPlusOffset(data_reg, base_reg, address - Rn);

    if (!MemUWrite(context, address, word, ebytes))
      return false;
  }
  return true;
}

// A8.6.309 VLD1 (single element to all lanes) This instruction loads one
// element from memory into every element of one or two vectors.
bool EmulateInstructionARM::EmulateVLD1SingleAll(const uint32_t opcode,
                                                 const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations(); CheckAdvSIMDEnabled(); NullCheckIfThumbEE(n);
        address = R[n]; if (address MOD alignment) != 0 then GenerateAlignmentException();
        if wback then R[n] = R[n] + (if register_index then R[m] else ebytes);
        replicated_element = Replicate(MemU[address,ebytes], elements);
        for r = 0 to regs-1
            D[d+r] = replicated_element;
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t ebytes;
    uint32_t elements;
    uint32_t regs;
    uint32_t alignment;
    uint32_t d;
    uint32_t n;
    uint32_t m;
    bool wback;
    bool register_index;

    switch (encoding) {
    case eEncodingT1:
    case eEncodingA1: {
      // if size == '11' || (size == '00' && a == '1') then UNDEFINED;
      uint32_t size = Bits32(opcode, 7, 6);
      if ((size == 3) || ((size == 0) && BitIsSet(opcode, 4)))
        return false;

      // ebytes = 1 << UInt(size); elements = 8 DIV ebytes; regs = if T == '0'
      // then 1 else 2;
      ebytes = 1 << size;
      elements = 8 / ebytes;
      if (BitIsClear(opcode, 5))
        regs = 1;
      else
        regs = 2;

      // alignment = if a == '0' then 1 else ebytes;
      if (BitIsClear(opcode, 4))
        alignment = 1;
      else
        alignment = ebytes;

      // d = UInt(D:Vd); n = UInt(Rn); m = UInt(Rm);
      d = (Bit32(opcode, 22) << 4) | Bits32(opcode, 15, 12);
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);

      // wback = (m != 15); register_index = (m != 15 && m != 13);
      wback = (m != 15);
      register_index = ((m != 15) && (m != 13));

      // if d+regs > 32 then UNPREDICTABLE; if n == 15 then UNPREDICTABLE;
      if ((d + regs) > 32)
        return false;

      if (n == 15)
        return false;
    } break;

    default:
      return false;
    }

    RegisterInfo base_reg;
    GetRegisterInfo(eRegisterKindDWARF, dwarf_r0 + n, base_reg);

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    // address = R[n]; if (address MOD alignment) != 0 then
    // GenerateAlignmentException();
    addr_t address = Rn;
    if ((address % alignment) != 0)
      return false;

    EmulateInstruction::Context context;
    // if wback then R[n] = R[n] + (if register_index then R[m] else ebytes);
    if (wback) {
      uint32_t Rm = ReadCoreReg(m, &success);
      if (!success)
        return false;

      uint32_t offset;
      if (register_index)
        offset = Rm;
      else
        offset = ebytes;

      context.type = eContextAdjustBaseRegister;
      context.SetRegisterPlusOffset(base_reg, offset);

      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_r0 + n,
                                 Rn + offset))
        return false;
    }

    // replicated_element = Replicate(MemU[address,ebytes], elements);

    context.type = eContextRegisterLoad;
    uint64_t word = MemURead(context, address, ebytes, 0, &success);
    if (!success)
      return false;

    uint64_t replicated_element = 0;
    uint32_t esize = ebytes * 8;
    for (uint32_t e = 0; e < elements; ++e)
      replicated_element =
          (replicated_element << esize) | Bits64(word, esize - 1, 0);

    // for r = 0 to regs-1
    for (uint32_t r = 0; r < regs; ++r) {
      // D[d+r] = replicated_element;
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_d0 + d + r,
                                 replicated_element))
        return false;
    }
  }
  return true;
}

// B6.2.13 SUBS PC, LR and related instructions The SUBS PC, LR, #<const?
// instruction provides an exception return without the use of the stack.  It
// subtracts the immediate constant from the LR, branches to the resulting
// address, and also copies the SPSR to the CPSR.
bool EmulateInstructionARM::EmulateSUBSPcLrEtc(const uint32_t opcode,
                                               const ARMEncoding encoding) {
#if 0
    if ConditionPassed() then
        EncodingSpecificOperations();
        if CurrentInstrSet() == InstrSet_ThumbEE then
            UNPREDICTABLE;
        operand2 = if register_form then Shift(R[m], shift_t, shift_n, APSR.C) else imm32;
        case opcode of
            when '0000' result = R[n] AND operand2; // AND
            when '0001' result = R[n] EOR operand2; // EOR
            when '0010' (result, -, -) = AddWithCarry(R[n], NOT(operand2), '1'); // SUB
            when '0011' (result, -, -) = AddWithCarry(NOT(R[n]), operand2, '1'); // RSB
            when '0100' (result, -, -) = AddWithCarry(R[n], operand2, '0'); // ADD
            when '0101' (result, -, -) = AddWithCarry(R[n], operand2, APSR.c); // ADC
            when '0110' (result, -, -) = AddWithCarry(R[n], NOT(operand2), APSR.C); // SBC
            when '0111' (result, -, -) = AddWithCarry(NOT(R[n]), operand2, APSR.C); // RSC
            when '1100' result = R[n] OR operand2; // ORR
            when '1101' result = operand2; // MOV
            when '1110' result = R[n] AND NOT(operand2); // BIC
            when '1111' result = NOT(operand2); // MVN
        CPSRWriteByInstr(SPSR[], '1111', TRUE);
        BranchWritePC(result);
#endif

  bool success = false;

  if (ConditionPassed(opcode)) {
    uint32_t n;
    uint32_t m;
    uint32_t imm32;
    bool register_form;
    ARM_ShifterType shift_t;
    uint32_t shift_n;
    uint32_t code;

    switch (encoding) {
    case eEncodingT1:
      // if CurrentInstrSet() == InstrSet_ThumbEE then UNPREDICTABLE n = 14;
      // imm32 = ZeroExtend(imm8, 32); register_form = FALSE; opcode = '0010';
      // // = SUB
      n = 14;
      imm32 = Bits32(opcode, 7, 0);
      register_form = false;
      code = 2;

      // if InITBlock() && !LastInITBlock() then UNPREDICTABLE;
      if (InITBlock() && !LastInITBlock())
        return false;

      break;

    case eEncodingA1:
      // n = UInt(Rn); imm32 = ARMExpandImm(imm12); register_form = FALSE;
      n = Bits32(opcode, 19, 16);
      imm32 = ARMExpandImm(opcode);
      register_form = false;
      code = Bits32(opcode, 24, 21);

      break;

    case eEncodingA2:
      // n = UInt(Rn); m = UInt(Rm); register_form = TRUE;
      n = Bits32(opcode, 19, 16);
      m = Bits32(opcode, 3, 0);
      register_form = true;

      // (shift_t, shift_n) = DecodeImmShift(type, imm5);
      shift_n = DecodeImmShiftARM(opcode, shift_t);

      break;

    default:
      return false;
    }

    // operand2 = if register_form then Shift(R[m], shift_t, shift_n, APSR.C)
    // else imm32;
    uint32_t operand2;
    if (register_form) {
      uint32_t Rm = ReadCoreReg(m, &success);
      if (!success)
        return false;

      operand2 = Shift(Rm, shift_t, shift_n, APSR_C, &success);
      if (!success)
        return false;
    } else {
      operand2 = imm32;
    }

    uint32_t Rn = ReadCoreReg(n, &success);
    if (!success)
      return false;

    AddWithCarryResult result;

    // case opcode of
    switch (code) {
    case 0: // when '0000'
      // result = R[n] AND operand2; // AND
      result.result = Rn & operand2;
      break;

    case 1: // when '0001'
      // result = R[n] EOR operand2; // EOR
      result.result = Rn ^ operand2;
      break;

    case 2: // when '0010'
      // (result, -, -) = AddWithCarry(R[n], NOT(operand2), '1'); // SUB
      result = AddWithCarry(Rn, ~(operand2), 1);
      break;

    case 3: // when '0011'
      // (result, -, -) = AddWithCarry(NOT(R[n]), operand2, '1'); // RSB
      result = AddWithCarry(~(Rn), operand2, 1);
      break;

    case 4: // when '0100'
      // (result, -, -) = AddWithCarry(R[n], operand2, '0'); // ADD
      result = AddWithCarry(Rn, operand2, 0);
      break;

    case 5: // when '0101'
      // (result, -, -) = AddWithCarry(R[n], operand2, APSR.c); // ADC
      result = AddWithCarry(Rn, operand2, APSR_C);
      break;

    case 6: // when '0110'
      // (result, -, -) = AddWithCarry(R[n], NOT(operand2), APSR.C); // SBC
      result = AddWithCarry(Rn, ~(operand2), APSR_C);
      break;

    case 7: // when '0111'
      // (result, -, -) = AddWithCarry(NOT(R[n]), operand2, APSR.C); // RSC
      result = AddWithCarry(~(Rn), operand2, APSR_C);
      break;

    case 10: // when '1100'
      // result = R[n] OR operand2; // ORR
      result.result = Rn | operand2;
      break;

    case 11: // when '1101'
      // result = operand2; // MOV
      result.result = operand2;
      break;

    case 12: // when '1110'
      // result = R[n] AND NOT(operand2); // BIC
      result.result = Rn & ~(operand2);
      break;

    case 15: // when '1111'
      // result = NOT(operand2); // MVN
      result.result = ~(operand2);
      break;

    default:
      return false;
    }
    // CPSRWriteByInstr(SPSR[], '1111', TRUE);

    // For now, in emulation mode, we don't have access to the SPSR, so we will
    // use the CPSR instead, and hope for the best.
    uint32_t spsr =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_cpsr, 0, &success);
    if (!success)
      return false;

    CPSRWriteByInstr(spsr, 15, true);

    // BranchWritePC(result);
    EmulateInstruction::Context context;
    context.type = eContextAdjustPC;
    context.SetImmediate(result.result);

    BranchWritePC(context, result.result);
  }
  return true;
}

EmulateInstructionARM::ARMOpcode *
EmulateInstructionARM::GetARMOpcodeForInstruction(const uint32_t opcode,
                                                  uint32_t arm_isa) {
  static ARMOpcode g_arm_opcodes[] = {
      //----------------------------------------------------------------------
      // Prologue instructions
      //----------------------------------------------------------------------

      // push register(s)
      {0x0fff0000, 0x092d0000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulatePUSH, "push <registers>"},
      {0x0fff0fff, 0x052d0004, ARMvAll, eEncodingA2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulatePUSH, "push <register>"},

      // set r7 to point to a stack offset
      {0x0ffff000, 0x028d7000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADDRdSPImm, "add r7, sp, #<const>"},
      {0x0ffff000, 0x024c7000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBR7IPImm, "sub r7, ip, #<const>"},
      // copy the stack pointer to ip
      {0x0fffffff, 0x01a0c00d, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMOVRdSP, "mov ip, sp"},
      {0x0ffff000, 0x028dc000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADDRdSPImm, "add ip, sp, #<const>"},
      {0x0ffff000, 0x024dc000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBIPSPImm, "sub ip, sp, #<const>"},

      // adjust the stack pointer
      {0x0ffff000, 0x024dd000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPImm, "sub sp, sp, #<const>"},
      {0x0fef0010, 0x004d0000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPReg,
       "sub{s}<c> <Rd>, sp, <Rm>{,<shift>}"},

      // push one register
      // if Rn == '1101' && imm12 == '000000000100' then SEE PUSH;
      {0x0e5f0000, 0x040d0000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRRtSP, "str Rt, [sp, #-imm12]!"},

      // vector push consecutive extension register(s)
      {0x0fbf0f00, 0x0d2d0b00, ARMV6T2_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateVPUSH, "vpush.64 <list>"},
      {0x0fbf0f00, 0x0d2d0a00, ARMV6T2_ABOVE, eEncodingA2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateVPUSH, "vpush.32 <list>"},

      //----------------------------------------------------------------------
      // Epilogue instructions
      //----------------------------------------------------------------------

      {0x0fff0000, 0x08bd0000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulatePOP, "pop <registers>"},
      {0x0fff0fff, 0x049d0004, ARMvAll, eEncodingA2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulatePOP, "pop <register>"},
      {0x0fbf0f00, 0x0cbd0b00, ARMV6T2_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateVPOP, "vpop.64 <list>"},
      {0x0fbf0f00, 0x0cbd0a00, ARMV6T2_ABOVE, eEncodingA2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateVPOP, "vpop.32 <list>"},

      //----------------------------------------------------------------------
      // Supervisor Call (previously Software Interrupt)
      //----------------------------------------------------------------------
      {0x0f000000, 0x0f000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSVC, "svc #imm24"},

      //----------------------------------------------------------------------
      // Branch instructions
      //----------------------------------------------------------------------
      // To resolve ambiguity, "blx <label>" should come before "b #imm24" and
      // "bl <label>".
      {0xfe000000, 0xfa000000, ARMV5_ABOVE, eEncodingA2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBLXImmediate, "blx <label>"},
      {0x0f000000, 0x0a000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateB, "b #imm24"},
      {0x0f000000, 0x0b000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBLXImmediate, "bl <label>"},
      {0x0ffffff0, 0x012fff30, ARMV5_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBLXRm, "blx <Rm>"},
      // for example, "bx lr"
      {0x0ffffff0, 0x012fff10, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBXRm, "bx <Rm>"},
      // bxj
      {0x0ffffff0, 0x012fff20, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBXJRm, "bxj <Rm>"},

      //----------------------------------------------------------------------
      // Data-processing instructions
      //----------------------------------------------------------------------
      // adc (immediate)
      {0x0fe00000, 0x02a00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADCImm, "adc{s}<c> <Rd>, <Rn>, #const"},
      // adc (register)
      {0x0fe00010, 0x00a00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADCReg,
       "adc{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // add (immediate)
      {0x0fe00000, 0x02800000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADDImmARM,
       "add{s}<c> <Rd>, <Rn>, #const"},
      // add (register)
      {0x0fe00010, 0x00800000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADDReg,
       "add{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // add (register-shifted register)
      {0x0fe00090, 0x00800010, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADDRegShift,
       "add{s}<c> <Rd>, <Rn>, <Rm>, <type> <RS>"},
      // adr
      {0x0fff0000, 0x028f0000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADR, "add<c> <Rd>, PC, #<const>"},
      {0x0fff0000, 0x024f0000, ARMvAll, eEncodingA2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADR, "sub<c> <Rd>, PC, #<const>"},
      // and (immediate)
      {0x0fe00000, 0x02000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateANDImm, "and{s}<c> <Rd>, <Rn>, #const"},
      // and (register)
      {0x0fe00010, 0x00000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateANDReg,
       "and{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // bic (immediate)
      {0x0fe00000, 0x03c00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBICImm, "bic{s}<c> <Rd>, <Rn>, #const"},
      // bic (register)
      {0x0fe00010, 0x01c00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBICReg,
       "bic{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // eor (immediate)
      {0x0fe00000, 0x02200000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateEORImm, "eor{s}<c> <Rd>, <Rn>, #const"},
      // eor (register)
      {0x0fe00010, 0x00200000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateEORReg,
       "eor{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // orr (immediate)
      {0x0fe00000, 0x03800000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateORRImm, "orr{s}<c> <Rd>, <Rn>, #const"},
      // orr (register)
      {0x0fe00010, 0x01800000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateORRReg,
       "orr{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // rsb (immediate)
      {0x0fe00000, 0x02600000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRSBImm, "rsb{s}<c> <Rd>, <Rn>, #<const>"},
      // rsb (register)
      {0x0fe00010, 0x00600000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRSBReg,
       "rsb{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // rsc (immediate)
      {0x0fe00000, 0x02e00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRSCImm, "rsc{s}<c> <Rd>, <Rn>, #<const>"},
      // rsc (register)
      {0x0fe00010, 0x00e00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRSCReg,
       "rsc{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // sbc (immediate)
      {0x0fe00000, 0x02c00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSBCImm, "sbc{s}<c> <Rd>, <Rn>, #<const>"},
      // sbc (register)
      {0x0fe00010, 0x00c00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSBCReg,
       "sbc{s}<c> <Rd>, <Rn>, <Rm> {,<shift>}"},
      // sub (immediate, ARM)
      {0x0fe00000, 0x02400000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBImmARM,
       "sub{s}<c> <Rd>, <Rn>, #<const>"},
      // sub (sp minus immediate)
      {0x0fef0000, 0x024d0000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPImm, "sub{s}<c> <Rd>, sp, #<const>"},
      // sub (register)
      {0x0fe00010, 0x00400000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBReg,
       "sub{s}<c> <Rd>, <Rn>, <Rm>{,<shift>}"},
      // teq (immediate)
      {0x0ff0f000, 0x03300000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTEQImm, "teq<c> <Rn>, #const"},
      // teq (register)
      {0x0ff0f010, 0x01300000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTEQReg, "teq<c> <Rn>, <Rm> {,<shift>}"},
      // tst (immediate)
      {0x0ff0f000, 0x03100000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTSTImm, "tst<c> <Rn>, #const"},
      // tst (register)
      {0x0ff0f010, 0x01100000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTSTReg, "tst<c> <Rn>, <Rm> {,<shift>}"},

      // mov (immediate)
      {0x0fef0000, 0x03a00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMOVRdImm, "mov{s}<c> <Rd>, #<const>"},
      {0x0ff00000, 0x03000000, ARMV6T2_ABOVE, eEncodingA2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMOVRdImm, "movw<c> <Rd>, #<imm16>"},
      // mov (register)
      {0x0fef0ff0, 0x01a00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMOVRdRm, "mov{s}<c> <Rd>, <Rm>"},
      // mvn (immediate)
      {0x0fef0000, 0x03e00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMVNImm, "mvn{s}<c> <Rd>, #<const>"},
      // mvn (register)
      {0x0fef0010, 0x01e00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMVNReg,
       "mvn{s}<c> <Rd>, <Rm> {,<shift>}"},
      // cmn (immediate)
      {0x0ff0f000, 0x03700000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateCMNImm, "cmn<c> <Rn>, #<const>"},
      // cmn (register)
      {0x0ff0f010, 0x01700000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateCMNReg, "cmn<c> <Rn>, <Rm> {,<shift>}"},
      // cmp (immediate)
      {0x0ff0f000, 0x03500000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateCMPImm, "cmp<c> <Rn>, #<const>"},
      // cmp (register)
      {0x0ff0f010, 0x01500000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateCMPReg, "cmp<c> <Rn>, <Rm> {,<shift>}"},
      // asr (immediate)
      {0x0fef0070, 0x01a00040, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateASRImm, "asr{s}<c> <Rd>, <Rm>, #imm"},
      // asr (register)
      {0x0fef00f0, 0x01a00050, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateASRReg, "asr{s}<c> <Rd>, <Rn>, <Rm>"},
      // lsl (immediate)
      {0x0fef0070, 0x01a00000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLSLImm, "lsl{s}<c> <Rd>, <Rm>, #imm"},
      // lsl (register)
      {0x0fef00f0, 0x01a00010, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLSLReg, "lsl{s}<c> <Rd>, <Rn>, <Rm>"},
      // lsr (immediate)
      {0x0fef0070, 0x01a00020, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLSRImm, "lsr{s}<c> <Rd>, <Rm>, #imm"},
      // lsr (register)
      {0x0fef00f0, 0x01a00050, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLSRReg, "lsr{s}<c> <Rd>, <Rn>, <Rm>"},
      // rrx is a special case encoding of ror (immediate)
      {0x0fef0ff0, 0x01a00060, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRRX, "rrx{s}<c> <Rd>, <Rm>"},
      // ror (immediate)
      {0x0fef0070, 0x01a00060, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRORImm, "ror{s}<c> <Rd>, <Rm>, #imm"},
      // ror (register)
      {0x0fef00f0, 0x01a00070, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRORReg, "ror{s}<c> <Rd>, <Rn>, <Rm>"},
      // mul
      {0x0fe000f0, 0x00000090, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMUL, "mul{s}<c> <Rd>,<R>,<Rm>"},

      // subs pc, lr and related instructions
      {0x0e10f000, 0x0210f000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPcLrEtc,
       "<opc>S<c> PC,#<const> | <Rn>,#<const>"},
      {0x0e10f010, 0x0010f000, ARMvAll, eEncodingA2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPcLrEtc,
       "<opc>S<c> PC,<Rn>,<Rm{,<shift>}"},

      //----------------------------------------------------------------------
      // Load instructions
      //----------------------------------------------------------------------
      {0x0fd00000, 0x08900000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDM, "ldm<c> <Rn>{!} <registers>"},
      {0x0fd00000, 0x08100000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDMDA, "ldmda<c> <Rn>{!} <registers>"},
      {0x0fd00000, 0x09100000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDMDB, "ldmdb<c> <Rn>{!} <registers>"},
      {0x0fd00000, 0x09900000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDMIB, "ldmib<c> <Rn<{!} <registers>"},
      {0x0e500000, 0x04100000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRImmediateARM,
       "ldr<c> <Rt> [<Rn> {#+/-<imm12>}]"},
      {0x0e500010, 0x06100000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRRegister,
       "ldr<c> <Rt> [<Rn> +/-<Rm> {<shift>}] {!}"},
      {0x0e5f0000, 0x045f0000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRBLiteral, "ldrb<c> <Rt>, [...]"},
      {0xfe500010, 0x06500000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRBRegister,
       "ldrb<c> <Rt>, [<Rn>,+/-<Rm>{, <shift>}]{!}"},
      {0x0e5f00f0, 0x005f00b0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRHLiteral, "ldrh<c> <Rt>, <label>"},
      {0x0e5000f0, 0x001000b0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRHRegister,
       "ldrh<c> <Rt>,[<Rn>,+/-<Rm>]{!}"},
      {0x0e5000f0, 0x005000d0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSBImmediate,
       "ldrsb<c> <Rt>, [<Rn>{,#+/-<imm8>}]"},
      {0x0e5f00f0, 0x005f00d0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSBLiteral, "ldrsb<c> <Rt> <label>"},
      {0x0e5000f0, 0x001000d0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSBRegister,
       "ldrsb<c> <Rt>,[<Rn>,+/-<Rm>]{!}"},
      {0x0e5000f0, 0x005000f0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSHImmediate,
       "ldrsh<c> <Rt>,[<Rn>{,#+/-<imm8>}]"},
      {0x0e5f00f0, 0x005f00f0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSHLiteral, "ldrsh<c> <Rt>,<label>"},
      {0x0e5000f0, 0x001000f0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSHRegister,
       "ldrsh<c> <Rt>,[<Rn>,+/-<Rm>]{!}"},
      {0x0e5000f0, 0x004000d0, ARMV5TE_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRDImmediate,
       "ldrd<c> <Rt>, <Rt2>, [<Rn>,#+/-<imm8>]!"},
      {0x0e500ff0, 0x000000d0, ARMV5TE_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRDRegister,
       "ldrd<c> <Rt>, <Rt2>, [<Rn>, +/-<Rm>]{!}"},
      {0x0e100f00, 0x0c100b00, ARMvAll, eEncodingA1, VFPv2_ABOVE, eSize32,
       &EmulateInstructionARM::EmulateVLDM, "vldm{mode}<c> <Rn>{!}, <list>"},
      {0x0e100f00, 0x0c100a00, ARMvAll, eEncodingA2, VFPv2v3, eSize32,
       &EmulateInstructionARM::EmulateVLDM, "vldm{mode}<c> <Rn>{!}, <list>"},
      {0x0f300f00, 0x0d100b00, ARMvAll, eEncodingA1, VFPv2_ABOVE, eSize32,
       &EmulateInstructionARM::EmulateVLDR, "vldr<c> <Dd>, [<Rn>{,#+/-<imm>}]"},
      {0x0f300f00, 0x0d100a00, ARMvAll, eEncodingA2, VFPv2v3, eSize32,
       &EmulateInstructionARM::EmulateVLDR, "vldr<c> <Sd>, [<Rn>{,#+/-<imm>}]"},
      {0xffb00000, 0xf4200000, ARMvAll, eEncodingA1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVLD1Multiple,
       "vld1<c>.<size> <list>, [<Rn>{@<align>}], <Rm>"},
      {0xffb00300, 0xf4a00000, ARMvAll, eEncodingA1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVLD1Single,
       "vld1<c>.<size> <list>, [<Rn>{@<align>}], <Rm>"},
      {0xffb00f00, 0xf4a00c00, ARMvAll, eEncodingA1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVLD1SingleAll,
       "vld1<c>.<size> <list>, [<Rn>{@<align>}], <Rm>"},

      //----------------------------------------------------------------------
      // Store instructions
      //----------------------------------------------------------------------
      {0x0fd00000, 0x08800000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTM, "stm<c> <Rn>{!} <registers>"},
      {0x0fd00000, 0x08000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTMDA, "stmda<c> <Rn>{!} <registers>"},
      {0x0fd00000, 0x09000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTMDB, "stmdb<c> <Rn>{!} <registers>"},
      {0x0fd00000, 0x09800000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTMIB, "stmib<c> <Rn>{!} <registers>"},
      {0x0e500010, 0x06000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRRegister,
       "str<c> <Rt> [<Rn> +/-<Rm> {<shift>}]{!}"},
      {0x0e5000f0, 0x000000b0, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRHRegister,
       "strh<c> <Rt>,[<Rn>,+/-<Rm>[{!}"},
      {0x0ff00ff0, 0x01800f90, ARMV6_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTREX, "strex<c> <Rd>, <Rt>, [<Rn>]"},
      {0x0e500000, 0x04400000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRBImmARM,
       "strb<c> <Rt>,[<Rn>,#+/-<imm12>]!"},
      {0x0e500000, 0x04000000, ARMvAll, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRImmARM,
       "str<c> <Rt>,[<Rn>,#+/-<imm12>]!"},
      {0x0e5000f0, 0x004000f0, ARMV5TE_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRDImm,
       "strd<c> <Rt>, <Rt2>, [<Rn> #+/-<imm8>]!"},
      {0x0e500ff0, 0x000000f0, ARMV5TE_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRDReg,
       "strd<c> <Rt>, <Rt2>, [<Rn>, +/-<Rm>]{!}"},
      {0x0e100f00, 0x0c000b00, ARMvAll, eEncodingA1, VFPv2_ABOVE, eSize32,
       &EmulateInstructionARM::EmulateVSTM, "vstm{mode}<c> <Rn>{!} <list>"},
      {0x0e100f00, 0x0c000a00, ARMvAll, eEncodingA2, VFPv2v3, eSize32,
       &EmulateInstructionARM::EmulateVSTM, "vstm{mode}<c> <Rn>{!} <list>"},
      {0x0f300f00, 0x0d000b00, ARMvAll, eEncodingA1, VFPv2_ABOVE, eSize32,
       &EmulateInstructionARM::EmulateVSTR, "vstr<c> <Dd> [<Rn>{,#+/-<imm>}]"},
      {0x0f300f00, 0x0d000a00, ARMvAll, eEncodingA2, VFPv2v3, eSize32,
       &EmulateInstructionARM::EmulateVSTR, "vstr<c> <Sd> [<Rn>{,#+/-<imm>}]"},
      {0xffb00000, 0xf4000000, ARMvAll, eEncodingA1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVST1Multiple,
       "vst1<c>.<size> <list>, [<Rn>{@<align>}], <Rm>"},
      {0xffb00300, 0xf4800000, ARMvAll, eEncodingA1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVST1Single,
       "vst1<c>.<size> <list>, [<Rn>{@<align>}], <Rm>"},

      //----------------------------------------------------------------------
      // Other instructions
      //----------------------------------------------------------------------
      {0x0fff00f0, 0x06af00f0, ARMV6_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSXTB, "sxtb<c> <Rd>,<Rm>{,<rotation>}"},
      {0x0fff00f0, 0x06bf0070, ARMV6_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSXTH, "sxth<c> <Rd>,<Rm>{,<rotation>}"},
      {0x0fff00f0, 0x06ef0070, ARMV6_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateUXTB, "uxtb<c> <Rd>,<Rm>{,<rotation>}"},
      {0x0fff00f0, 0x06ff0070, ARMV6_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateUXTH, "uxth<c> <Rd>,<Rm>{,<rotation>}"},
      {0xfe500000, 0xf8100000, ARMV6_ABOVE, eEncodingA1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRFE, "rfe{<amode>} <Rn>{!}"}

  };
  static const size_t k_num_arm_opcodes = llvm::array_lengthof(g_arm_opcodes);

  for (size_t i = 0; i < k_num_arm_opcodes; ++i) {
    if ((g_arm_opcodes[i].mask & opcode) == g_arm_opcodes[i].value &&
        (g_arm_opcodes[i].variants & arm_isa) != 0)
      return &g_arm_opcodes[i];
  }
  return NULL;
}

EmulateInstructionARM::ARMOpcode *
EmulateInstructionARM::GetThumbOpcodeForInstruction(const uint32_t opcode,
                                                    uint32_t arm_isa) {

  static ARMOpcode g_thumb_opcodes[] = {
      //----------------------------------------------------------------------
      // Prologue instructions
      //----------------------------------------------------------------------

      // push register(s)
      {0xfffffe00, 0x0000b400, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulatePUSH, "push <registers>"},
      {0xffff0000, 0xe92d0000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulatePUSH, "push.w <registers>"},
      {0xffff0fff, 0xf84d0d04, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulatePUSH, "push.w <register>"},

      // set r7 to point to a stack offset
      {0xffffff00, 0x0000af00, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADDRdSPImm, "add r7, sp, #imm"},
      // copy the stack pointer to r7
      {0xffffffff, 0x0000466f, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateMOVRdSP, "mov r7, sp"},
      // move from high register to low register (comes after "mov r7, sp" to
      // resolve ambiguity)
      {0xffffffc0, 0x00004640, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateMOVLowHigh, "mov r0-r7, r8-r15"},

      // PC-relative load into register (see also EmulateADDSPRm)
      {0xfffff800, 0x00004800, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRRtPCRelative, "ldr <Rt>, [PC, #imm]"},

      // adjust the stack pointer
      {0xffffff87, 0x00004485, ARMvAll, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADDSPRm, "add sp, <Rm>"},
      {0xffffff80, 0x0000b080, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSUBSPImm, "sub sp, sp, #imm"},
      {0xfbef8f00, 0xf1ad0d00, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPImm, "sub.w sp, sp, #<const>"},
      {0xfbff8f00, 0xf2ad0d00, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPImm, "subw sp, sp, #imm12"},
      {0xffef8000, 0xebad0000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPReg,
       "sub{s}<c> <Rd>, sp, <Rm>{,<shift>}"},

      // vector push consecutive extension register(s)
      {0xffbf0f00, 0xed2d0b00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateVPUSH, "vpush.64 <list>"},
      {0xffbf0f00, 0xed2d0a00, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateVPUSH, "vpush.32 <list>"},

      //----------------------------------------------------------------------
      // Epilogue instructions
      //----------------------------------------------------------------------

      {0xfffff800, 0x0000a800, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADDSPImm, "add<c> <Rd>, sp, #imm"},
      {0xffffff80, 0x0000b000, ARMvAll, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADDSPImm, "add sp, #imm"},
      {0xfffffe00, 0x0000bc00, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulatePOP, "pop <registers>"},
      {0xffff0000, 0xe8bd0000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulatePOP, "pop.w <registers>"},
      {0xffff0fff, 0xf85d0d04, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulatePOP, "pop.w <register>"},
      {0xffbf0f00, 0xecbd0b00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateVPOP, "vpop.64 <list>"},
      {0xffbf0f00, 0xecbd0a00, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateVPOP, "vpop.32 <list>"},

      //----------------------------------------------------------------------
      // Supervisor Call (previously Software Interrupt)
      //----------------------------------------------------------------------
      {0xffffff00, 0x0000df00, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSVC, "svc #imm8"},

      //----------------------------------------------------------------------
      // If Then makes up to four following instructions conditional.
      //----------------------------------------------------------------------
      // The next 5 opcode _must_ come before the if then instruction
      {0xffffffff, 0x0000bf00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateNop, "nop"},
      {0xffffffff, 0x0000bf10, ARMV7_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateNop, "nop YIELD (yield hint)"},
      {0xffffffff, 0x0000bf20, ARMV7_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateNop, "nop WFE (wait for event hint)"},
      {0xffffffff, 0x0000bf30, ARMV7_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateNop, "nop WFI (wait for interrupt hint)"},
      {0xffffffff, 0x0000bf40, ARMV7_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateNop, "nop SEV (send event hint)"},
      {0xffffff00, 0x0000bf00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateIT, "it{<x>{<y>{<z>}}} <firstcond>"},

      //----------------------------------------------------------------------
      // Branch instructions
      //----------------------------------------------------------------------
      // To resolve ambiguity, "b<c> #imm8" should come after "svc #imm8".
      {0xfffff000, 0x0000d000, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateB, "b<c> #imm8 (outside IT)"},
      {0xfffff800, 0x0000e000, ARMvAll, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateB, "b<c> #imm11 (outside or last in IT)"},
      {0xf800d000, 0xf0008000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateB, "b<c>.w #imm8 (outside IT)"},
      {0xf800d000, 0xf0009000, ARMV6T2_ABOVE, eEncodingT4, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateB,
       "b<c>.w #imm8 (outside or last in IT)"},
      // J1 == J2 == 1
      {0xf800d000, 0xf000d000, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBLXImmediate, "bl <label>"},
      // J1 == J2 == 1
      {0xf800d001, 0xf000c000, ARMV5_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBLXImmediate, "blx <label>"},
      {0xffffff87, 0x00004780, ARMV5_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateBLXRm, "blx <Rm>"},
      // for example, "bx lr"
      {0xffffff87, 0x00004700, ARMvAll, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBXRm, "bx <Rm>"},
      // bxj
      {0xfff0ffff, 0xf3c08f00, ARMV5J_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBXJRm, "bxj <Rm>"},
      // compare and branch
      {0xfffff500, 0x0000b100, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateCB, "cb{n}z <Rn>, <label>"},
      // table branch byte
      {0xfff0fff0, 0xe8d0f000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTB, "tbb<c> <Rn>, <Rm>"},
      // table branch halfword
      {0xfff0fff0, 0xe8d0f010, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTB, "tbh<c> <Rn>, <Rm>, lsl #1"},

      //----------------------------------------------------------------------
      // Data-processing instructions
      //----------------------------------------------------------------------
      // adc (immediate)
      {0xfbe08000, 0xf1400000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADCImm, "adc{s}<c> <Rd>, <Rn>, #<const>"},
      // adc (register)
      {0xffffffc0, 0x00004140, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADCReg, "adcs|adc<c> <Rdn>, <Rm>"},
      {0xffe08000, 0xeb400000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADCReg,
       "adc{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
      // add (register)
      {0xfffffe00, 0x00001800, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADDReg, "adds|add<c> <Rd>, <Rn>, <Rm>"},
      // Make sure "add sp, <Rm>" comes before this instruction, so there's no
      // ambiguity decoding the two.
      {0xffffff00, 0x00004400, ARMvAll, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADDReg, "add<c> <Rdn>, <Rm>"},
      // adr
      {0xfffff800, 0x0000a000, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADR, "add<c> <Rd>, PC, #<const>"},
      {0xfbff8000, 0xf2af0000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADR, "sub<c> <Rd>, PC, #<const>"},
      {0xfbff8000, 0xf20f0000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADR, "add<c> <Rd>, PC, #<const>"},
      // and (immediate)
      {0xfbe08000, 0xf0000000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateANDImm, "and{s}<c> <Rd>, <Rn>, #<const>"},
      // and (register)
      {0xffffffc0, 0x00004000, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateANDReg, "ands|and<c> <Rdn>, <Rm>"},
      {0xffe08000, 0xea000000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateANDReg,
       "and{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
      // bic (immediate)
      {0xfbe08000, 0xf0200000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBICImm, "bic{s}<c> <Rd>, <Rn>, #<const>"},
      // bic (register)
      {0xffffffc0, 0x00004380, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateBICReg, "bics|bic<c> <Rdn>, <Rm>"},
      {0xffe08000, 0xea200000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateBICReg,
       "bic{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
      // eor (immediate)
      {0xfbe08000, 0xf0800000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateEORImm, "eor{s}<c> <Rd>, <Rn>, #<const>"},
      // eor (register)
      {0xffffffc0, 0x00004040, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateEORReg, "eors|eor<c> <Rdn>, <Rm>"},
      {0xffe08000, 0xea800000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateEORReg,
       "eor{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
      // orr (immediate)
      {0xfbe08000, 0xf0400000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateORRImm, "orr{s}<c> <Rd>, <Rn>, #<const>"},
      // orr (register)
      {0xffffffc0, 0x00004300, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateORRReg, "orrs|orr<c> <Rdn>, <Rm>"},
      {0xffe08000, 0xea400000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateORRReg,
       "orr{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
      // rsb (immediate)
      {0xffffffc0, 0x00004240, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateRSBImm, "rsbs|rsb<c> <Rd>, <Rn>, #0"},
      {0xfbe08000, 0xf1c00000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRSBImm,
       "rsb{s}<c>.w <Rd>, <Rn>, #<const>"},
      // rsb (register)
      {0xffe08000, 0xea400000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRSBReg,
       "rsb{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
      // sbc (immediate)
      {0xfbe08000, 0xf1600000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSBCImm, "sbc{s}<c> <Rd>, <Rn>, #<const>"},
      // sbc (register)
      {0xffffffc0, 0x00004180, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSBCReg, "sbcs|sbc<c> <Rdn>, <Rm>"},
      {0xffe08000, 0xeb600000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSBCReg,
       "sbc{s}<c>.w <Rd>, <Rn>, <Rm> {,<shift>}"},
      // add (immediate, Thumb)
      {0xfffffe00, 0x00001c00, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADDImmThumb,
       "adds|add<c> <Rd>,<Rn>,#<imm3>"},
      {0xfffff800, 0x00003000, ARMV4T_ABOVE, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateADDImmThumb, "adds|add<c> <Rdn>,#<imm8>"},
      {0xfbe08000, 0xf1000000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADDImmThumb,
       "add{s}<c>.w <Rd>,<Rn>,#<const>"},
      {0xfbf08000, 0xf2000000, ARMV6T2_ABOVE, eEncodingT4, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateADDImmThumb,
       "addw<c> <Rd>,<Rn>,#<imm12>"},
      // sub (immediate, Thumb)
      {0xfffffe00, 0x00001e00, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSUBImmThumb,
       "subs|sub<c> <Rd>, <Rn> #imm3"},
      {0xfffff800, 0x00003800, ARMvAll, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSUBImmThumb, "subs|sub<c> <Rdn>, #imm8"},
      {0xfbe08000, 0xf1a00000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBImmThumb,
       "sub{s}<c>.w <Rd>, <Rn>, #<const>"},
      {0xfbf08000, 0xf2a00000, ARMV6T2_ABOVE, eEncodingT4, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBImmThumb,
       "subw<c> <Rd>, <Rn>, #imm12"},
      // sub (sp minus immediate)
      {0xfbef8000, 0xf1ad0000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPImm, "sub{s}.w <Rd>, sp, #<const>"},
      {0xfbff8000, 0xf2ad0000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPImm, "subw<c> <Rd>, sp, #imm12"},
      // sub (register)
      {0xfffffe00, 0x00001a00, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSUBReg, "subs|sub<c> <Rd>, <Rn>, <Rm>"},
      {0xffe08000, 0xeba00000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBReg,
       "sub{s}<c>.w <Rd>, <Rn>, <Rm>{,<shift>}"},
      // teq (immediate)
      {0xfbf08f00, 0xf0900f00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTEQImm, "teq<c> <Rn>, #<const>"},
      // teq (register)
      {0xfff08f00, 0xea900f00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTEQReg, "teq<c> <Rn>, <Rm> {,<shift>}"},
      // tst (immediate)
      {0xfbf08f00, 0xf0100f00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTSTImm, "tst<c> <Rn>, #<const>"},
      // tst (register)
      {0xffffffc0, 0x00004200, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateTSTReg, "tst<c> <Rdn>, <Rm>"},
      {0xfff08f00, 0xea100f00, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateTSTReg, "tst<c>.w <Rn>, <Rm> {,<shift>}"},

      // move from high register to high register
      {0xffffff00, 0x00004600, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateMOVRdRm, "mov<c> <Rd>, <Rm>"},
      // move from low register to low register
      {0xffffffc0, 0x00000000, ARMvAll, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateMOVRdRm, "movs <Rd>, <Rm>"},
      // mov{s}<c>.w <Rd>, <Rm>
      {0xffeff0f0, 0xea4f0000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMOVRdRm, "mov{s}<c>.w <Rd>, <Rm>"},
      // move immediate
      {0xfffff800, 0x00002000, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateMOVRdImm, "movs|mov<c> <Rd>, #imm8"},
      {0xfbef8000, 0xf04f0000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMOVRdImm, "mov{s}<c>.w <Rd>, #<const>"},
      {0xfbf08000, 0xf2400000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMOVRdImm, "movw<c> <Rd>,#<imm16>"},
      // mvn (immediate)
      {0xfbef8000, 0xf06f0000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMVNImm, "mvn{s} <Rd>, #<const>"},
      // mvn (register)
      {0xffffffc0, 0x000043c0, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateMVNReg, "mvns|mvn<c> <Rd>, <Rm>"},
      {0xffef8000, 0xea6f0000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMVNReg,
       "mvn{s}<c>.w <Rd>, <Rm> {,<shift>}"},
      // cmn (immediate)
      {0xfbf08f00, 0xf1100f00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateCMNImm, "cmn<c> <Rn>, #<const>"},
      // cmn (register)
      {0xffffffc0, 0x000042c0, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateCMNReg, "cmn<c> <Rn>, <Rm>"},
      {0xfff08f00, 0xeb100f00, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateCMNReg, "cmn<c> <Rn>, <Rm> {,<shift>}"},
      // cmp (immediate)
      {0xfffff800, 0x00002800, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateCMPImm, "cmp<c> <Rn>, #imm8"},
      {0xfbf08f00, 0xf1b00f00, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateCMPImm, "cmp<c>.w <Rn>, #<const>"},
      // cmp (register) (Rn and Rm both from r0-r7)
      {0xffffffc0, 0x00004280, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateCMPReg, "cmp<c> <Rn>, <Rm>"},
      // cmp (register) (Rn and Rm not both from r0-r7)
      {0xffffff00, 0x00004500, ARMvAll, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateCMPReg, "cmp<c> <Rn>, <Rm>"},
      {0xfff08f00, 0xebb00f00, ARMvAll, eEncodingT3, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateCMPReg,
       "cmp<c>.w <Rn>, <Rm> {, <shift>}"},
      // asr (immediate)
      {0xfffff800, 0x00001000, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateASRImm, "asrs|asr<c> <Rd>, <Rm>, #imm"},
      {0xffef8030, 0xea4f0020, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateASRImm, "asr{s}<c>.w <Rd>, <Rm>, #imm"},
      // asr (register)
      {0xffffffc0, 0x00004100, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateASRReg, "asrs|asr<c> <Rdn>, <Rm>"},
      {0xffe0f0f0, 0xfa40f000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateASRReg, "asr{s}<c>.w <Rd>, <Rn>, <Rm>"},
      // lsl (immediate)
      {0xfffff800, 0x00000000, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLSLImm, "lsls|lsl<c> <Rd>, <Rm>, #imm"},
      {0xffef8030, 0xea4f0000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLSLImm, "lsl{s}<c>.w <Rd>, <Rm>, #imm"},
      // lsl (register)
      {0xffffffc0, 0x00004080, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLSLReg, "lsls|lsl<c> <Rdn>, <Rm>"},
      {0xffe0f0f0, 0xfa00f000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLSLReg, "lsl{s}<c>.w <Rd>, <Rn>, <Rm>"},
      // lsr (immediate)
      {0xfffff800, 0x00000800, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLSRImm, "lsrs|lsr<c> <Rd>, <Rm>, #imm"},
      {0xffef8030, 0xea4f0010, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLSRImm, "lsr{s}<c>.w <Rd>, <Rm>, #imm"},
      // lsr (register)
      {0xffffffc0, 0x000040c0, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLSRReg, "lsrs|lsr<c> <Rdn>, <Rm>"},
      {0xffe0f0f0, 0xfa20f000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLSRReg, "lsr{s}<c>.w <Rd>, <Rn>, <Rm>"},
      // rrx is a special case encoding of ror (immediate)
      {0xffeff0f0, 0xea4f0030, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRRX, "rrx{s}<c>.w <Rd>, <Rm>"},
      // ror (immediate)
      {0xffef8030, 0xea4f0030, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRORImm, "ror{s}<c>.w <Rd>, <Rm>, #imm"},
      // ror (register)
      {0xffffffc0, 0x000041c0, ARMvAll, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateRORReg, "rors|ror<c> <Rdn>, <Rm>"},
      {0xffe0f0f0, 0xfa60f000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRORReg, "ror{s}<c>.w <Rd>, <Rn>, <Rm>"},
      // mul
      {0xffffffc0, 0x00004340, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateMUL, "muls <Rdm>,<Rn>,<Rdm>"},
      // mul
      {0xfff0f0f0, 0xfb00f000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateMUL, "mul<c> <Rd>,<Rn>,<Rm>"},

      // subs pc, lr and related instructions
      {0xffffff00, 0xf3de8f00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSUBSPcLrEtc, "SUBS<c> PC, LR, #<imm8>"},

      //----------------------------------------------------------------------
      // RFE instructions  *** IMPORTANT *** THESE MUST BE LISTED **BEFORE** THE
      // LDM.. Instructions in this table;
      // otherwise the wrong instructions will be selected.
      //----------------------------------------------------------------------

      {0xffd0ffff, 0xe810c000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRFE, "rfedb<c> <Rn>{!}"},
      {0xffd0ffff, 0xe990c000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateRFE, "rfe{ia}<c> <Rn>{!}"},

      //----------------------------------------------------------------------
      // Load instructions
      //----------------------------------------------------------------------
      {0xfffff800, 0x0000c800, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDM, "ldm<c> <Rn>{!} <registers>"},
      {0xffd02000, 0xe8900000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDM, "ldm<c>.w <Rn>{!} <registers>"},
      {0xffd00000, 0xe9100000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDMDB, "ldmdb<c> <Rn>{!} <registers>"},
      {0xfffff800, 0x00006800, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRRtRnImm, "ldr<c> <Rt>, [<Rn>{,#imm}]"},
      {0xfffff800, 0x00009800, ARMV4T_ABOVE, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRRtRnImm, "ldr<c> <Rt>, [SP{,#imm}]"},
      {0xfff00000, 0xf8d00000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRRtRnImm,
       "ldr<c>.w <Rt>, [<Rn>{,#imm12}]"},
      {0xfff00800, 0xf8500800, ARMV6T2_ABOVE, eEncodingT4, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRRtRnImm,
       "ldr<c> <Rt>, [<Rn>{,#+/-<imm8>}]{!}"},
      // Thumb2 PC-relative load into register
      {0xff7f0000, 0xf85f0000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRRtPCRelative,
       "ldr<c>.w <Rt>, [PC, +/-#imm}]"},
      {0xfffffe00, 0x00005800, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRRegister, "ldr<c> <Rt>, [<Rn>, <Rm>]"},
      {0xfff00fc0, 0xf8500000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRRegister,
       "ldr<c>.w <Rt>, [<Rn>,<Rm>{,LSL #<imm2>}]"},
      {0xfffff800, 0x00007800, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRBImmediate,
       "ldrb<c> <Rt>,[<Rn>{,#<imm5>}]"},
      {0xfff00000, 0xf8900000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRBImmediate,
       "ldrb<c>.w <Rt>,[<Rn>{,#<imm12>}]"},
      {0xfff00800, 0xf8100800, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRBImmediate,
       "ldrb<c> <Rt>,[<Rn>, #+/-<imm8>]{!}"},
      {0xff7f0000, 0xf81f0000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRBLiteral, "ldrb<c> <Rt>,[...]"},
      {0xfffffe00, 0x00005c00, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRBRegister, "ldrb<c> <Rt>,[<Rn>,<Rm>]"},
      {0xfff00fc0, 0xf8100000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRBRegister,
       "ldrb<c>.w <Rt>,[<Rn>,<Rm>{,LSL #imm2>}]"},
      {0xfffff800, 0x00008800, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRHImmediate,
       "ldrh<c> <Rt>, [<Rn>{,#<imm>}]"},
      {0xfff00000, 0xf8b00000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRHImmediate,
       "ldrh<c>.w <Rt>,[<Rn>{,#<imm12>}]"},
      {0xfff00800, 0xf8300800, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRHImmediate,
       "ldrh<c> <Rt>,[<Rn>,#+/-<imm8>]{!}"},
      {0xff7f0000, 0xf83f0000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRHLiteral, "ldrh<c> <Rt>, <label>"},
      {0xfffffe00, 0x00005a00, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRHRegister,
       "ldrh<c> <Rt>, [<Rn>,<Rm>]"},
      {0xfff00fc0, 0xf8300000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRHRegister,
       "ldrh<c>.w <Rt>,[<Rn>,<Rm>{,LSL #<imm2>}]"},
      {0xfff00000, 0xf9900000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSBImmediate,
       "ldrsb<c> <Rt>,[<Rn>,#<imm12>]"},
      {0xfff00800, 0xf9100800, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSBImmediate,
       "ldrsb<c> <Rt>,[<Rn>,#+/-<imm8>]"},
      {0xff7f0000, 0xf91f0000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSBLiteral, "ldrsb<c> <Rt>, <label>"},
      {0xfffffe00, 0x00005600, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRSBRegister,
       "ldrsb<c> <Rt>,[<Rn>,<Rm>]"},
      {0xfff00fc0, 0xf9100000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSBRegister,
       "ldrsb<c>.w <Rt>,[<Rn>,<Rm>{,LSL #imm2>}]"},
      {0xfff00000, 0xf9b00000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSHImmediate,
       "ldrsh<c> <Rt>,[<Rn>,#<imm12>]"},
      {0xfff00800, 0xf9300800, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSHImmediate,
       "ldrsh<c> <Rt>,[<Rn>,#+/-<imm8>]"},
      {0xff7f0000, 0xf93f0000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSHLiteral, "ldrsh<c> <Rt>,<label>"},
      {0xfffffe00, 0x00005e00, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateLDRSHRegister,
       "ldrsh<c> <Rt>,[<Rn>,<Rm>]"},
      {0xfff00fc0, 0xf9300000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRSHRegister,
       "ldrsh<c>.w <Rt>,[<Rn>,<Rm>{,LSL #<imm2>}]"},
      {0xfe500000, 0xe8500000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateLDRDImmediate,
       "ldrd<c> <Rt>, <Rt2>, [<Rn>,#+/-<imm>]!"},
      {0xfe100f00, 0xec100b00, ARMvAll, eEncodingT1, VFPv2_ABOVE, eSize32,
       &EmulateInstructionARM::EmulateVLDM, "vldm{mode}<c> <Rn>{!}, <list>"},
      {0xfe100f00, 0xec100a00, ARMvAll, eEncodingT2, VFPv2v3, eSize32,
       &EmulateInstructionARM::EmulateVLDM, "vldm{mode}<c> <Rn>{!}, <list>"},
      {0xffe00f00, 0xed100b00, ARMvAll, eEncodingT1, VFPv2_ABOVE, eSize32,
       &EmulateInstructionARM::EmulateVLDR, "vldr<c> <Dd>, [<Rn>{,#+/-<imm>}]"},
      {0xff300f00, 0xed100a00, ARMvAll, eEncodingT2, VFPv2v3, eSize32,
       &EmulateInstructionARM::EmulateVLDR, "vldr<c> <Sd>, {<Rn>{,#+/-<imm>}]"},
      {0xffb00000, 0xf9200000, ARMvAll, eEncodingT1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVLD1Multiple,
       "vld1<c>.<size> <list>, [<Rn>{@<align>}],<Rm>"},
      {0xffb00300, 0xf9a00000, ARMvAll, eEncodingT1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVLD1Single,
       "vld1<c>.<size> <list>, [<Rn>{@<align>}],<Rm>"},
      {0xffb00f00, 0xf9a00c00, ARMvAll, eEncodingT1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVLD1SingleAll,
       "vld1<c>.<size> <list>, [<Rn>{@<align>}], <Rm>"},

      //----------------------------------------------------------------------
      // Store instructions
      //----------------------------------------------------------------------
      {0xfffff800, 0x0000c000, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSTM, "stm<c> <Rn>{!} <registers>"},
      {0xffd00000, 0xe8800000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTM, "stm<c>.w <Rn>{!} <registers>"},
      {0xffd00000, 0xe9000000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTMDB, "stmdb<c> <Rn>{!} <registers>"},
      {0xfffff800, 0x00006000, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSTRThumb, "str<c> <Rt>, [<Rn>{,#<imm>}]"},
      {0xfffff800, 0x00009000, ARMV4T_ABOVE, eEncodingT2, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSTRThumb, "str<c> <Rt>, [SP,#<imm>]"},
      {0xfff00000, 0xf8c00000, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRThumb,
       "str<c>.w <Rt>, [<Rn>,#<imm12>]"},
      {0xfff00800, 0xf8400800, ARMV6T2_ABOVE, eEncodingT4, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRThumb,
       "str<c> <Rt>, [<Rn>,#+/-<imm8>]"},
      {0xfffffe00, 0x00005000, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSTRRegister, "str<c> <Rt> ,{<Rn>, <Rm>]"},
      {0xfff00fc0, 0xf8400000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRRegister,
       "str<c>.w <Rt>, [<Rn>, <Rm> {lsl #imm2>}]"},
      {0xfffff800, 0x00007000, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSTRBThumb,
       "strb<c> <Rt>, [<Rn>, #<imm5>]"},
      {0xfff00000, 0xf8800000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRBThumb,
       "strb<c>.w <Rt>, [<Rn>, #<imm12>]"},
      {0xfff00800, 0xf8000800, ARMV6T2_ABOVE, eEncodingT3, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRBThumb,
       "strb<c> <Rt> ,[<Rn>, #+/-<imm8>]{!}"},
      {0xfffffe00, 0x00005200, ARMV4T_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSTRHRegister, "strh<c> <Rt>,[<Rn>,<Rm>]"},
      {0xfff00fc0, 0xf8200000, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRHRegister,
       "strh<c>.w <Rt>,[<Rn>,<Rm>{,LSL #<imm2>}]"},
      {0xfff00000, 0xe8400000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTREX,
       "strex<c> <Rd>, <Rt>, [<Rn{,#<imm>}]"},
      {0xfe500000, 0xe8400000, ARMV6T2_ABOVE, eEncodingT1, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSTRDImm,
       "strd<c> <Rt>, <Rt2>, [<Rn>, #+/-<imm>]!"},
      {0xfe100f00, 0xec000b00, ARMvAll, eEncodingT1, VFPv2_ABOVE, eSize32,
       &EmulateInstructionARM::EmulateVSTM, "vstm{mode}<c> <Rn>{!}, <list>"},
      {0xfea00f00, 0xec000a00, ARMvAll, eEncodingT2, VFPv2v3, eSize32,
       &EmulateInstructionARM::EmulateVSTM, "vstm{mode}<c> <Rn>{!}, <list>"},
      {0xff300f00, 0xed000b00, ARMvAll, eEncodingT1, VFPv2_ABOVE, eSize32,
       &EmulateInstructionARM::EmulateVSTR, "vstr<c> <Dd>, [<Rn>{,#+/-<imm>}]"},
      {0xff300f00, 0xed000a00, ARMvAll, eEncodingT2, VFPv2v3, eSize32,
       &EmulateInstructionARM::EmulateVSTR, "vstr<c> <Sd>, [<Rn>{,#+/-<imm>}]"},
      {0xffb00000, 0xf9000000, ARMvAll, eEncodingT1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVST1Multiple,
       "vst1<c>.<size> <list>, [<Rn>{@<align>}], <Rm>"},
      {0xffb00300, 0xf9800000, ARMvAll, eEncodingT1, AdvancedSIMD, eSize32,
       &EmulateInstructionARM::EmulateVST1Single,
       "vst1<c>.<size> <list>, [<Rn>{@<align>}], <Rm>"},

      //----------------------------------------------------------------------
      // Other instructions
      //----------------------------------------------------------------------
      {0xffffffc0, 0x0000b240, ARMV6_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSXTB, "sxtb<c> <Rd>,<Rm>"},
      {0xfffff080, 0xfa4ff080, ARMV6_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSXTB, "sxtb<c>.w <Rd>,<Rm>{,<rotation>}"},
      {0xffffffc0, 0x0000b200, ARMV6_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateSXTH, "sxth<c> <Rd>,<Rm>"},
      {0xfffff080, 0xfa0ff080, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateSXTH, "sxth<c>.w <Rd>,<Rm>{,<rotation>}"},
      {0xffffffc0, 0x0000b2c0, ARMV6_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateUXTB, "uxtb<c> <Rd>,<Rm>"},
      {0xfffff080, 0xfa5ff080, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateUXTB, "uxtb<c>.w <Rd>,<Rm>{,<rotation>}"},
      {0xffffffc0, 0x0000b280, ARMV6_ABOVE, eEncodingT1, No_VFP, eSize16,
       &EmulateInstructionARM::EmulateUXTH, "uxth<c> <Rd>,<Rm>"},
      {0xfffff080, 0xfa1ff080, ARMV6T2_ABOVE, eEncodingT2, No_VFP, eSize32,
       &EmulateInstructionARM::EmulateUXTH, "uxth<c>.w <Rd>,<Rm>{,<rotation>}"},
  };

  const size_t k_num_thumb_opcodes = llvm::array_lengthof(g_thumb_opcodes);
  for (size_t i = 0; i < k_num_thumb_opcodes; ++i) {
    if ((g_thumb_opcodes[i].mask & opcode) == g_thumb_opcodes[i].value &&
        (g_thumb_opcodes[i].variants & arm_isa) != 0)
      return &g_thumb_opcodes[i];
  }
  return NULL;
}

bool EmulateInstructionARM::SetArchitecture(const ArchSpec &arch) {
  m_arch = arch;
  m_arm_isa = 0;
  const char *arch_cstr = arch.GetArchitectureName();
  if (arch_cstr) {
    if (0 == ::strcasecmp(arch_cstr, "armv4t"))
      m_arm_isa = ARMv4T;
    else if (0 == ::strcasecmp(arch_cstr, "armv5tej"))
      m_arm_isa = ARMv5TEJ;
    else if (0 == ::strcasecmp(arch_cstr, "armv5te"))
      m_arm_isa = ARMv5TE;
    else if (0 == ::strcasecmp(arch_cstr, "armv5t"))
      m_arm_isa = ARMv5T;
    else if (0 == ::strcasecmp(arch_cstr, "armv6k"))
      m_arm_isa = ARMv6K;
    else if (0 == ::strcasecmp(arch_cstr, "armv6t2"))
      m_arm_isa = ARMv6T2;
    else if (0 == ::strcasecmp(arch_cstr, "armv7s"))
      m_arm_isa = ARMv7S;
    else if (0 == ::strcasecmp(arch_cstr, "arm"))
      m_arm_isa = ARMvAll;
    else if (0 == ::strcasecmp(arch_cstr, "thumb"))
      m_arm_isa = ARMvAll;
    else if (0 == ::strncasecmp(arch_cstr, "armv4", 5))
      m_arm_isa = ARMv4;
    else if (0 == ::strncasecmp(arch_cstr, "armv6", 5))
      m_arm_isa = ARMv6;
    else if (0 == ::strncasecmp(arch_cstr, "armv7", 5))
      m_arm_isa = ARMv7;
    else if (0 == ::strncasecmp(arch_cstr, "armv8", 5))
      m_arm_isa = ARMv8;
  }
  return m_arm_isa != 0;
}

bool EmulateInstructionARM::SetInstruction(const Opcode &insn_opcode,
                                           const Address &inst_addr,
                                           Target *target) {
  if (EmulateInstruction::SetInstruction(insn_opcode, inst_addr, target)) {
    if (m_arch.GetTriple().getArch() == llvm::Triple::thumb ||
        m_arch.IsAlwaysThumbInstructions())
      m_opcode_mode = eModeThumb;
    else {
      AddressClass addr_class = inst_addr.GetAddressClass();

      if ((addr_class == AddressClass::eCode) ||
          (addr_class == AddressClass::eUnknown))
        m_opcode_mode = eModeARM;
      else if (addr_class == AddressClass::eCodeAlternateISA)
        m_opcode_mode = eModeThumb;
      else
        return false;
    }
    if (m_opcode_mode == eModeThumb || m_arch.IsAlwaysThumbInstructions())
      m_opcode_cpsr = CPSR_MODE_USR | MASK_CPSR_T;
    else
      m_opcode_cpsr = CPSR_MODE_USR;
    return true;
  }
  return false;
}

bool EmulateInstructionARM::ReadInstruction() {
  bool success = false;
  m_opcode_cpsr = ReadRegisterUnsigned(eRegisterKindGeneric,
                                       LLDB_REGNUM_GENERIC_FLAGS, 0, &success);
  if (success) {
    addr_t pc =
        ReadRegisterUnsigned(eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC,
                             LLDB_INVALID_ADDRESS, &success);
    if (success) {
      Context read_inst_context;
      read_inst_context.type = eContextReadOpcode;
      read_inst_context.SetNoArgs();

      if ((m_opcode_cpsr & MASK_CPSR_T) || m_arch.IsAlwaysThumbInstructions()) {
        m_opcode_mode = eModeThumb;
        uint32_t thumb_opcode = MemARead(read_inst_context, pc, 2, 0, &success);

        if (success) {
          if ((thumb_opcode & 0xe000) != 0xe000 ||
              ((thumb_opcode & 0x1800u) == 0)) {
            m_opcode.SetOpcode16(thumb_opcode, GetByteOrder());
          } else {
            m_opcode.SetOpcode32(
                (thumb_opcode << 16) |
                    MemARead(read_inst_context, pc + 2, 2, 0, &success),
                GetByteOrder());
          }
        }
      } else {
        m_opcode_mode = eModeARM;
        m_opcode.SetOpcode32(MemARead(read_inst_context, pc, 4, 0, &success),
                             GetByteOrder());
      }

      if (!m_ignore_conditions) {
        // If we are not ignoreing the conditions then init the it session from
        // the current value of cpsr.
        uint32_t it = (Bits32(m_opcode_cpsr, 15, 10) << 2) |
                      Bits32(m_opcode_cpsr, 26, 25);
        if (it != 0)
          m_it_session.InitIT(it);
      }
    }
  }
  if (!success) {
    m_opcode_mode = eModeInvalid;
    m_addr = LLDB_INVALID_ADDRESS;
  }
  return success;
}

uint32_t EmulateInstructionARM::ArchVersion() { return m_arm_isa; }

bool EmulateInstructionARM::ConditionPassed(const uint32_t opcode) {
  // If we are ignoring conditions, then always return true. this allows us to
  // iterate over disassembly code and still emulate an instruction even if we
  // don't have all the right bits set in the CPSR register...
  if (m_ignore_conditions)
    return true;

  const uint32_t cond = CurrentCond(opcode);
  if (cond == UINT32_MAX)
    return false;

  bool result = false;
  switch (UnsignedBits(cond, 3, 1)) {
  case 0:
    if (m_opcode_cpsr == 0)
      result = true;
    else
      result = (m_opcode_cpsr & MASK_CPSR_Z) != 0;
    break;
  case 1:
    if (m_opcode_cpsr == 0)
      result = true;
    else
      result = (m_opcode_cpsr & MASK_CPSR_C) != 0;
    break;
  case 2:
    if (m_opcode_cpsr == 0)
      result = true;
    else
      result = (m_opcode_cpsr & MASK_CPSR_N) != 0;
    break;
  case 3:
    if (m_opcode_cpsr == 0)
      result = true;
    else
      result = (m_opcode_cpsr & MASK_CPSR_V) != 0;
    break;
  case 4:
    if (m_opcode_cpsr == 0)
      result = true;
    else
      result = ((m_opcode_cpsr & MASK_CPSR_C) != 0) &&
               ((m_opcode_cpsr & MASK_CPSR_Z) == 0);
    break;
  case 5:
    if (m_opcode_cpsr == 0)
      result = true;
    else {
      bool n = (m_opcode_cpsr & MASK_CPSR_N);
      bool v = (m_opcode_cpsr & MASK_CPSR_V);
      result = n == v;
    }
    break;
  case 6:
    if (m_opcode_cpsr == 0)
      result = true;
    else {
      bool n = (m_opcode_cpsr & MASK_CPSR_N);
      bool v = (m_opcode_cpsr & MASK_CPSR_V);
      result = n == v && ((m_opcode_cpsr & MASK_CPSR_Z) == 0);
    }
    break;
  case 7:
    // Always execute (cond == 0b1110, or the special 0b1111 which gives
    // opcodes different meanings, but always means execution happens.
    return true;
  }

  if (cond & 1)
    result = !result;
  return result;
}

uint32_t EmulateInstructionARM::CurrentCond(const uint32_t opcode) {
  switch (m_opcode_mode) {
  case eModeInvalid:
    break;

  case eModeARM:
    return UnsignedBits(opcode, 31, 28);

  case eModeThumb:
    // For T1 and T3 encodings of the Branch instruction, it returns the 4-bit
    // 'cond' field of the encoding.
    {
      const uint32_t byte_size = m_opcode.GetByteSize();
      if (byte_size == 2) {
        if (Bits32(opcode, 15, 12) == 0x0d && Bits32(opcode, 11, 8) != 0x0f)
          return Bits32(opcode, 11, 8);
      } else if (byte_size == 4) {
        if (Bits32(opcode, 31, 27) == 0x1e && Bits32(opcode, 15, 14) == 0x02 &&
            Bits32(opcode, 12, 12) == 0x00 && Bits32(opcode, 25, 22) <= 0x0d) {
          return Bits32(opcode, 25, 22);
        }
      } else
        // We have an invalid thumb instruction, let's bail out.
        break;

      return m_it_session.GetCond();
    }
  }
  return UINT32_MAX; // Return invalid value
}

bool EmulateInstructionARM::InITBlock() {
  return CurrentInstrSet() == eModeThumb && m_it_session.InITBlock();
}

bool EmulateInstructionARM::LastInITBlock() {
  return CurrentInstrSet() == eModeThumb && m_it_session.LastInITBlock();
}

bool EmulateInstructionARM::BadMode(uint32_t mode) {

  switch (mode) {
  case 16:
    return false; // '10000'
  case 17:
    return false; // '10001'
  case 18:
    return false; // '10010'
  case 19:
    return false; // '10011'
  case 22:
    return false; // '10110'
  case 23:
    return false; // '10111'
  case 27:
    return false; // '11011'
  case 31:
    return false; // '11111'
  default:
    return true;
  }
  return true;
}

bool EmulateInstructionARM::CurrentModeIsPrivileged() {
  uint32_t mode = Bits32(m_opcode_cpsr, 4, 0);

  if (BadMode(mode))
    return false;

  if (mode == 16)
    return false;

  return true;
}

void EmulateInstructionARM::CPSRWriteByInstr(uint32_t value, uint32_t bytemask,
                                             bool affect_execstate) {
  bool privileged = CurrentModeIsPrivileged();

  uint32_t tmp_cpsr = Bits32(m_opcode_cpsr, 23, 20) << 20;

  if (BitIsSet(bytemask, 3)) {
    tmp_cpsr = tmp_cpsr | (Bits32(value, 31, 27) << 27);
    if (affect_execstate)
      tmp_cpsr = tmp_cpsr | (Bits32(value, 26, 24) << 24);
  }

  if (BitIsSet(bytemask, 2)) {
    tmp_cpsr = tmp_cpsr | (Bits32(value, 19, 16) << 16);
  }

  if (BitIsSet(bytemask, 1)) {
    if (affect_execstate)
      tmp_cpsr = tmp_cpsr | (Bits32(value, 15, 10) << 10);
    tmp_cpsr = tmp_cpsr | (Bit32(value, 9) << 9);
    if (privileged)
      tmp_cpsr = tmp_cpsr | (Bit32(value, 8) << 8);
  }

  if (BitIsSet(bytemask, 0)) {
    if (privileged)
      tmp_cpsr = tmp_cpsr | (Bits32(value, 7, 6) << 6);
    if (affect_execstate)
      tmp_cpsr = tmp_cpsr | (Bit32(value, 5) << 5);
    if (privileged)
      tmp_cpsr = tmp_cpsr | Bits32(value, 4, 0);
  }

  m_opcode_cpsr = tmp_cpsr;
}

bool EmulateInstructionARM::BranchWritePC(const Context &context,
                                          uint32_t addr) {
  addr_t target;

  // Check the current instruction set.
  if (CurrentInstrSet() == eModeARM)
    target = addr & 0xfffffffc;
  else
    target = addr & 0xfffffffe;

  return WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_PC, target);
}

// As a side effect, BXWritePC sets context.arg2 to eModeARM or eModeThumb by
// inspecting addr.
bool EmulateInstructionARM::BXWritePC(Context &context, uint32_t addr) {
  addr_t target;
  // If the CPSR is changed due to switching between ARM and Thumb ISETSTATE,
  // we want to record it and issue a WriteRegister callback so the clients can
  // track the mode changes accordingly.
  bool cpsr_changed = false;

  if (BitIsSet(addr, 0)) {
    if (CurrentInstrSet() != eModeThumb) {
      SelectInstrSet(eModeThumb);
      cpsr_changed = true;
    }
    target = addr & 0xfffffffe;
    context.SetISA(eModeThumb);
  } else if (BitIsClear(addr, 1)) {
    if (CurrentInstrSet() != eModeARM) {
      SelectInstrSet(eModeARM);
      cpsr_changed = true;
    }
    target = addr & 0xfffffffc;
    context.SetISA(eModeARM);
  } else
    return false; // address<1:0> == '10' => UNPREDICTABLE

  if (cpsr_changed) {
    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_FLAGS, m_new_inst_cpsr))
      return false;
  }
  return WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_PC, target);
}

// Dispatches to either BXWritePC or BranchWritePC based on architecture
// versions.
bool EmulateInstructionARM::LoadWritePC(Context &context, uint32_t addr) {
  if (ArchVersion() >= ARMv5T)
    return BXWritePC(context, addr);
  else
    return BranchWritePC((const Context)context, addr);
}

// Dispatches to either BXWritePC or BranchWritePC based on architecture
// versions and current instruction set.
bool EmulateInstructionARM::ALUWritePC(Context &context, uint32_t addr) {
  if (ArchVersion() >= ARMv7 && CurrentInstrSet() == eModeARM)
    return BXWritePC(context, addr);
  else
    return BranchWritePC((const Context)context, addr);
}

EmulateInstructionARM::Mode EmulateInstructionARM::CurrentInstrSet() {
  return m_opcode_mode;
}

// Set the 'T' bit of our CPSR.  The m_opcode_mode gets updated when the next
// ReadInstruction() is performed.  This function has a side effect of updating
// the m_new_inst_cpsr member variable if necessary.
bool EmulateInstructionARM::SelectInstrSet(Mode arm_or_thumb) {
  m_new_inst_cpsr = m_opcode_cpsr;
  switch (arm_or_thumb) {
  default:
    return false;
  case eModeARM:
    // Clear the T bit.
    m_new_inst_cpsr &= ~MASK_CPSR_T;
    break;
  case eModeThumb:
    // Set the T bit.
    m_new_inst_cpsr |= MASK_CPSR_T;
    break;
  }
  return true;
}

// This function returns TRUE if the processor currently provides support for
// unaligned memory accesses, or FALSE otherwise. This is always TRUE in ARMv7,
// controllable by the SCTLR.U bit in ARMv6, and always FALSE before ARMv6.
bool EmulateInstructionARM::UnalignedSupport() {
  return (ArchVersion() >= ARMv7);
}

// The main addition and subtraction instructions can produce status
// information about both unsigned carry and signed overflow conditions.  This
// status information can be used to synthesize multi-word additions and
// subtractions.
EmulateInstructionARM::AddWithCarryResult
EmulateInstructionARM::AddWithCarry(uint32_t x, uint32_t y, uint8_t carry_in) {
  uint32_t result;
  uint8_t carry_out;
  uint8_t overflow;

  uint64_t unsigned_sum = x + y + carry_in;
  int64_t signed_sum = (int32_t)x + (int32_t)y + (int32_t)carry_in;

  result = UnsignedBits(unsigned_sum, 31, 0);
  //    carry_out = (result == unsigned_sum ? 0 : 1);
  overflow = ((int32_t)result == signed_sum ? 0 : 1);

  if (carry_in)
    carry_out = ((int32_t)x >= (int32_t)(~y)) ? 1 : 0;
  else
    carry_out = ((int32_t)x > (int32_t)y) ? 1 : 0;

  AddWithCarryResult res = {result, carry_out, overflow};
  return res;
}

uint32_t EmulateInstructionARM::ReadCoreReg(uint32_t num, bool *success) {
  lldb::RegisterKind reg_kind;
  uint32_t reg_num;
  switch (num) {
  case SP_REG:
    reg_kind = eRegisterKindGeneric;
    reg_num = LLDB_REGNUM_GENERIC_SP;
    break;
  case LR_REG:
    reg_kind = eRegisterKindGeneric;
    reg_num = LLDB_REGNUM_GENERIC_RA;
    break;
  case PC_REG:
    reg_kind = eRegisterKindGeneric;
    reg_num = LLDB_REGNUM_GENERIC_PC;
    break;
  default:
    if (num < SP_REG) {
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_r0 + num;
    } else {
      // assert(0 && "Invalid register number");
      *success = false;
      return UINT32_MAX;
    }
    break;
  }

  // Read our register.
  uint32_t val = ReadRegisterUnsigned(reg_kind, reg_num, 0, success);

  // When executing an ARM instruction , PC reads as the address of the current
  // instruction plus 8. When executing a Thumb instruction , PC reads as the
  // address of the current instruction plus 4.
  if (num == 15) {
    if (CurrentInstrSet() == eModeARM)
      val += 8;
    else
      val += 4;
  }

  return val;
}

// Write the result to the ARM core register Rd, and optionally update the
// condition flags based on the result.
//
// This helper method tries to encapsulate the following pseudocode from the
// ARM Architecture Reference Manual:
//
// if d == 15 then         // Can only occur for encoding A1
//     ALUWritePC(result); // setflags is always FALSE here
// else
//     R[d] = result;
//     if setflags then
//         APSR.N = result<31>;
//         APSR.Z = IsZeroBit(result);
//         APSR.C = carry;
//         // APSR.V unchanged
//
// In the above case, the API client does not pass in the overflow arg, which
// defaults to ~0u.
bool EmulateInstructionARM::WriteCoreRegOptionalFlags(
    Context &context, const uint32_t result, const uint32_t Rd, bool setflags,
    const uint32_t carry, const uint32_t overflow) {
  if (Rd == 15) {
    if (!ALUWritePC(context, result))
      return false;
  } else {
    lldb::RegisterKind reg_kind;
    uint32_t reg_num;
    switch (Rd) {
    case SP_REG:
      reg_kind = eRegisterKindGeneric;
      reg_num = LLDB_REGNUM_GENERIC_SP;
      break;
    case LR_REG:
      reg_kind = eRegisterKindGeneric;
      reg_num = LLDB_REGNUM_GENERIC_RA;
      break;
    default:
      reg_kind = eRegisterKindDWARF;
      reg_num = dwarf_r0 + Rd;
    }
    if (!WriteRegisterUnsigned(context, reg_kind, reg_num, result))
      return false;
    if (setflags)
      return WriteFlags(context, result, carry, overflow);
  }
  return true;
}

// This helper method tries to encapsulate the following pseudocode from the
// ARM Architecture Reference Manual:
//
// APSR.N = result<31>;
// APSR.Z = IsZeroBit(result);
// APSR.C = carry;
// APSR.V = overflow
//
// Default arguments can be specified for carry and overflow parameters, which
// means not to update the respective flags.
bool EmulateInstructionARM::WriteFlags(Context &context, const uint32_t result,
                                       const uint32_t carry,
                                       const uint32_t overflow) {
  m_new_inst_cpsr = m_opcode_cpsr;
  SetBit32(m_new_inst_cpsr, CPSR_N_POS, Bit32(result, CPSR_N_POS));
  SetBit32(m_new_inst_cpsr, CPSR_Z_POS, result == 0 ? 1 : 0);
  if (carry != ~0u)
    SetBit32(m_new_inst_cpsr, CPSR_C_POS, carry);
  if (overflow != ~0u)
    SetBit32(m_new_inst_cpsr, CPSR_V_POS, overflow);
  if (m_new_inst_cpsr != m_opcode_cpsr) {
    if (!WriteRegisterUnsigned(context, eRegisterKindGeneric,
                               LLDB_REGNUM_GENERIC_FLAGS, m_new_inst_cpsr))
      return false;
  }
  return true;
}

bool EmulateInstructionARM::EvaluateInstruction(uint32_t evaluate_options) {
  ARMOpcode *opcode_data = NULL;

  if (m_opcode_mode == eModeThumb)
    opcode_data =
        GetThumbOpcodeForInstruction(m_opcode.GetOpcode32(), m_arm_isa);
  else if (m_opcode_mode == eModeARM)
    opcode_data = GetARMOpcodeForInstruction(m_opcode.GetOpcode32(), m_arm_isa);

  const bool auto_advance_pc =
      evaluate_options & eEmulateInstructionOptionAutoAdvancePC;
  m_ignore_conditions =
      evaluate_options & eEmulateInstructionOptionIgnoreConditions;

  bool success = false;
  if (m_opcode_cpsr == 0 || !m_ignore_conditions) {
    m_opcode_cpsr =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_cpsr, 0, &success);
  }

  // Only return false if we are unable to read the CPSR if we care about
  // conditions
  if (!success && !m_ignore_conditions)
    return false;

  uint32_t orig_pc_value = 0;
  if (auto_advance_pc) {
    orig_pc_value =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc, 0, &success);
    if (!success)
      return false;
  }

  // Call the Emulate... function if we managed to decode the opcode.
  if (opcode_data) {
    success = (this->*opcode_data->callback)(m_opcode.GetOpcode32(),
                                             opcode_data->encoding);
    if (!success)
      return false;
  }

  // Advance the ITSTATE bits to their values for the next instruction if we
  // haven't just executed an IT instruction what initialized it.
  if (m_opcode_mode == eModeThumb && m_it_session.InITBlock() &&
      (opcode_data == nullptr ||
       opcode_data->callback != &EmulateInstructionARM::EmulateIT))
    m_it_session.ITAdvance();

  if (auto_advance_pc) {
    uint32_t after_pc_value =
        ReadRegisterUnsigned(eRegisterKindDWARF, dwarf_pc, 0, &success);
    if (!success)
      return false;

    if (auto_advance_pc && (after_pc_value == orig_pc_value)) {
      after_pc_value += m_opcode.GetByteSize();

      EmulateInstruction::Context context;
      context.type = eContextAdvancePC;
      context.SetNoArgs();
      if (!WriteRegisterUnsigned(context, eRegisterKindDWARF, dwarf_pc,
                                 after_pc_value))
        return false;
    }
  }
  return true;
}

EmulateInstruction::InstructionCondition
EmulateInstructionARM::GetInstructionCondition() {
  const uint32_t cond = CurrentCond(m_opcode.GetOpcode32());
  if (cond == 0xe || cond == 0xf || cond == UINT32_MAX)
    return EmulateInstruction::UnconditionalCondition;
  return cond;
}

bool EmulateInstructionARM::TestEmulation(Stream *out_stream, ArchSpec &arch,
                                          OptionValueDictionary *test_data) {
  if (!test_data) {
    out_stream->Printf("TestEmulation: Missing test data.\n");
    return false;
  }

  static ConstString opcode_key("opcode");
  static ConstString before_key("before_state");
  static ConstString after_key("after_state");

  OptionValueSP value_sp = test_data->GetValueForKey(opcode_key);

  uint32_t test_opcode;
  if ((value_sp.get() == NULL) ||
      (value_sp->GetType() != OptionValue::eTypeUInt64)) {
    out_stream->Printf("TestEmulation: Error reading opcode from test file.\n");
    return false;
  }
  test_opcode = value_sp->GetUInt64Value();

  if (arch.GetTriple().getArch() == llvm::Triple::thumb ||
      arch.IsAlwaysThumbInstructions()) {
    m_opcode_mode = eModeThumb;
    if (test_opcode < 0x10000)
      m_opcode.SetOpcode16(test_opcode, endian::InlHostByteOrder());
    else
      m_opcode.SetOpcode32(test_opcode, endian::InlHostByteOrder());
  } else if (arch.GetTriple().getArch() == llvm::Triple::arm) {
    m_opcode_mode = eModeARM;
    m_opcode.SetOpcode32(test_opcode, endian::InlHostByteOrder());
  } else {
    out_stream->Printf("TestEmulation:  Invalid arch.\n");
    return false;
  }

  EmulationStateARM before_state;
  EmulationStateARM after_state;

  value_sp = test_data->GetValueForKey(before_key);
  if ((value_sp.get() == NULL) ||
      (value_sp->GetType() != OptionValue::eTypeDictionary)) {
    out_stream->Printf("TestEmulation:  Failed to find 'before' state.\n");
    return false;
  }

  OptionValueDictionary *state_dictionary = value_sp->GetAsDictionary();
  if (!before_state.LoadStateFromDictionary(state_dictionary)) {
    out_stream->Printf("TestEmulation:  Failed loading 'before' state.\n");
    return false;
  }

  value_sp = test_data->GetValueForKey(after_key);
  if ((value_sp.get() == NULL) ||
      (value_sp->GetType() != OptionValue::eTypeDictionary)) {
    out_stream->Printf("TestEmulation:  Failed to find 'after' state.\n");
    return false;
  }

  state_dictionary = value_sp->GetAsDictionary();
  if (!after_state.LoadStateFromDictionary(state_dictionary)) {
    out_stream->Printf("TestEmulation: Failed loading 'after' state.\n");
    return false;
  }

  SetBaton((void *)&before_state);
  SetCallbacks(&EmulationStateARM::ReadPseudoMemory,
               &EmulationStateARM::WritePseudoMemory,
               &EmulationStateARM::ReadPseudoRegister,
               &EmulationStateARM::WritePseudoRegister);

  bool success = EvaluateInstruction(eEmulateInstructionOptionAutoAdvancePC);
  if (!success) {
    out_stream->Printf("TestEmulation:  EvaluateInstruction() failed.\n");
    return false;
  }

  success = before_state.CompareState(after_state);
  if (!success)
    out_stream->Printf(
        "TestEmulation:  'before' and 'after' states do not match.\n");

  return success;
}
//
//
// const char *
// EmulateInstructionARM::GetRegisterName (uint32_t reg_kind, uint32_t reg_num)
//{
//    if (reg_kind == eRegisterKindGeneric)
//    {
//        switch (reg_num)
//        {
//        case LLDB_REGNUM_GENERIC_PC:    return "pc";
//        case LLDB_REGNUM_GENERIC_SP:    return "sp";
//        case LLDB_REGNUM_GENERIC_FP:    return "fp";
//        case LLDB_REGNUM_GENERIC_RA:    return "lr";
//        case LLDB_REGNUM_GENERIC_FLAGS: return "cpsr";
//        default: return NULL;
//        }
//    }
//    else if (reg_kind == eRegisterKindDWARF)
//    {
//        return GetARMDWARFRegisterName (reg_num);
//    }
//    return NULL;
//}
//
bool EmulateInstructionARM::CreateFunctionEntryUnwind(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  UnwindPlan::RowSP row(new UnwindPlan::Row);

  // Our previous Call Frame Address is the stack pointer
  row->GetCFAValue().SetIsRegisterPlusOffset(dwarf_sp, 0);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("EmulateInstructionARM");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  unwind_plan.SetReturnAddressRegister(dwarf_lr);
  return true;
}
