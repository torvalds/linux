//===-- x86AssemblyInspectionEngine.cpp -------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "x86AssemblyInspectionEngine.h"

#include "llvm-c/Disassembler.h"

#include "lldb/Core/Address.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/UnwindAssembly.h"

using namespace lldb_private;
using namespace lldb;

x86AssemblyInspectionEngine::x86AssemblyInspectionEngine(const ArchSpec &arch)
    : m_cur_insn(nullptr), m_machine_ip_regnum(LLDB_INVALID_REGNUM),
      m_machine_sp_regnum(LLDB_INVALID_REGNUM),
      m_machine_fp_regnum(LLDB_INVALID_REGNUM),
      m_lldb_ip_regnum(LLDB_INVALID_REGNUM),
      m_lldb_sp_regnum(LLDB_INVALID_REGNUM),
      m_lldb_fp_regnum(LLDB_INVALID_REGNUM),

      m_reg_map(), m_arch(arch), m_cpu(k_cpu_unspecified), m_wordsize(-1),
      m_register_map_initialized(false), m_disasm_context() {
  m_disasm_context =
      ::LLVMCreateDisasm(arch.GetTriple().getTriple().c_str(), nullptr,
                         /*TagType=*/1, nullptr, nullptr);
}

x86AssemblyInspectionEngine::~x86AssemblyInspectionEngine() {
  ::LLVMDisasmDispose(m_disasm_context);
}

void x86AssemblyInspectionEngine::Initialize(RegisterContextSP &reg_ctx) {
  m_cpu = k_cpu_unspecified;
  m_wordsize = -1;
  m_register_map_initialized = false;

  const llvm::Triple::ArchType cpu = m_arch.GetMachine();
  if (cpu == llvm::Triple::x86)
    m_cpu = k_i386;
  else if (cpu == llvm::Triple::x86_64)
    m_cpu = k_x86_64;

  if (m_cpu == k_cpu_unspecified)
    return;

  if (reg_ctx.get() == nullptr)
    return;

  if (m_cpu == k_i386) {
    m_machine_ip_regnum = k_machine_eip;
    m_machine_sp_regnum = k_machine_esp;
    m_machine_fp_regnum = k_machine_ebp;
    m_machine_alt_fp_regnum = k_machine_ebx;
    m_wordsize = 4;

    struct lldb_reg_info reginfo;
    reginfo.name = "eax";
    m_reg_map[k_machine_eax] = reginfo;
    reginfo.name = "edx";
    m_reg_map[k_machine_edx] = reginfo;
    reginfo.name = "esp";
    m_reg_map[k_machine_esp] = reginfo;
    reginfo.name = "esi";
    m_reg_map[k_machine_esi] = reginfo;
    reginfo.name = "eip";
    m_reg_map[k_machine_eip] = reginfo;
    reginfo.name = "ecx";
    m_reg_map[k_machine_ecx] = reginfo;
    reginfo.name = "ebx";
    m_reg_map[k_machine_ebx] = reginfo;
    reginfo.name = "ebp";
    m_reg_map[k_machine_ebp] = reginfo;
    reginfo.name = "edi";
    m_reg_map[k_machine_edi] = reginfo;
  } else {
    m_machine_ip_regnum = k_machine_rip;
    m_machine_sp_regnum = k_machine_rsp;
    m_machine_fp_regnum = k_machine_rbp;
    m_machine_alt_fp_regnum = k_machine_rbx;
    m_wordsize = 8;

    struct lldb_reg_info reginfo;
    reginfo.name = "rax";
    m_reg_map[k_machine_rax] = reginfo;
    reginfo.name = "rdx";
    m_reg_map[k_machine_rdx] = reginfo;
    reginfo.name = "rsp";
    m_reg_map[k_machine_rsp] = reginfo;
    reginfo.name = "rsi";
    m_reg_map[k_machine_rsi] = reginfo;
    reginfo.name = "r8";
    m_reg_map[k_machine_r8] = reginfo;
    reginfo.name = "r10";
    m_reg_map[k_machine_r10] = reginfo;
    reginfo.name = "r12";
    m_reg_map[k_machine_r12] = reginfo;
    reginfo.name = "r14";
    m_reg_map[k_machine_r14] = reginfo;
    reginfo.name = "rip";
    m_reg_map[k_machine_rip] = reginfo;
    reginfo.name = "rcx";
    m_reg_map[k_machine_rcx] = reginfo;
    reginfo.name = "rbx";
    m_reg_map[k_machine_rbx] = reginfo;
    reginfo.name = "rbp";
    m_reg_map[k_machine_rbp] = reginfo;
    reginfo.name = "rdi";
    m_reg_map[k_machine_rdi] = reginfo;
    reginfo.name = "r9";
    m_reg_map[k_machine_r9] = reginfo;
    reginfo.name = "r11";
    m_reg_map[k_machine_r11] = reginfo;
    reginfo.name = "r13";
    m_reg_map[k_machine_r13] = reginfo;
    reginfo.name = "r15";
    m_reg_map[k_machine_r15] = reginfo;
  }

  for (MachineRegnumToNameAndLLDBRegnum::iterator it = m_reg_map.begin();
       it != m_reg_map.end(); ++it) {
    const RegisterInfo *ri = reg_ctx->GetRegisterInfoByName(it->second.name);
    if (ri)
      it->second.lldb_regnum = ri->kinds[eRegisterKindLLDB];
  }

  uint32_t lldb_regno;
  if (machine_regno_to_lldb_regno(m_machine_sp_regnum, lldb_regno))
    m_lldb_sp_regnum = lldb_regno;
  if (machine_regno_to_lldb_regno(m_machine_fp_regnum, lldb_regno))
    m_lldb_fp_regnum = lldb_regno;
  if (machine_regno_to_lldb_regno(m_machine_alt_fp_regnum, lldb_regno))
    m_lldb_alt_fp_regnum = lldb_regno;
  if (machine_regno_to_lldb_regno(m_machine_ip_regnum, lldb_regno))
    m_lldb_ip_regnum = lldb_regno;

  m_register_map_initialized = true;
}

void x86AssemblyInspectionEngine::Initialize(
    std::vector<lldb_reg_info> &reg_info) {
  m_cpu = k_cpu_unspecified;
  m_wordsize = -1;
  m_register_map_initialized = false;

  const llvm::Triple::ArchType cpu = m_arch.GetMachine();
  if (cpu == llvm::Triple::x86)
    m_cpu = k_i386;
  else if (cpu == llvm::Triple::x86_64)
    m_cpu = k_x86_64;

  if (m_cpu == k_cpu_unspecified)
    return;

  if (m_cpu == k_i386) {
    m_machine_ip_regnum = k_machine_eip;
    m_machine_sp_regnum = k_machine_esp;
    m_machine_fp_regnum = k_machine_ebp;
    m_machine_alt_fp_regnum = k_machine_ebx;
    m_wordsize = 4;

    struct lldb_reg_info reginfo;
    reginfo.name = "eax";
    m_reg_map[k_machine_eax] = reginfo;
    reginfo.name = "edx";
    m_reg_map[k_machine_edx] = reginfo;
    reginfo.name = "esp";
    m_reg_map[k_machine_esp] = reginfo;
    reginfo.name = "esi";
    m_reg_map[k_machine_esi] = reginfo;
    reginfo.name = "eip";
    m_reg_map[k_machine_eip] = reginfo;
    reginfo.name = "ecx";
    m_reg_map[k_machine_ecx] = reginfo;
    reginfo.name = "ebx";
    m_reg_map[k_machine_ebx] = reginfo;
    reginfo.name = "ebp";
    m_reg_map[k_machine_ebp] = reginfo;
    reginfo.name = "edi";
    m_reg_map[k_machine_edi] = reginfo;
  } else {
    m_machine_ip_regnum = k_machine_rip;
    m_machine_sp_regnum = k_machine_rsp;
    m_machine_fp_regnum = k_machine_rbp;
    m_machine_alt_fp_regnum = k_machine_rbx;
    m_wordsize = 8;

    struct lldb_reg_info reginfo;
    reginfo.name = "rax";
    m_reg_map[k_machine_rax] = reginfo;
    reginfo.name = "rdx";
    m_reg_map[k_machine_rdx] = reginfo;
    reginfo.name = "rsp";
    m_reg_map[k_machine_rsp] = reginfo;
    reginfo.name = "rsi";
    m_reg_map[k_machine_rsi] = reginfo;
    reginfo.name = "r8";
    m_reg_map[k_machine_r8] = reginfo;
    reginfo.name = "r10";
    m_reg_map[k_machine_r10] = reginfo;
    reginfo.name = "r12";
    m_reg_map[k_machine_r12] = reginfo;
    reginfo.name = "r14";
    m_reg_map[k_machine_r14] = reginfo;
    reginfo.name = "rip";
    m_reg_map[k_machine_rip] = reginfo;
    reginfo.name = "rcx";
    m_reg_map[k_machine_rcx] = reginfo;
    reginfo.name = "rbx";
    m_reg_map[k_machine_rbx] = reginfo;
    reginfo.name = "rbp";
    m_reg_map[k_machine_rbp] = reginfo;
    reginfo.name = "rdi";
    m_reg_map[k_machine_rdi] = reginfo;
    reginfo.name = "r9";
    m_reg_map[k_machine_r9] = reginfo;
    reginfo.name = "r11";
    m_reg_map[k_machine_r11] = reginfo;
    reginfo.name = "r13";
    m_reg_map[k_machine_r13] = reginfo;
    reginfo.name = "r15";
    m_reg_map[k_machine_r15] = reginfo;
  }

  for (MachineRegnumToNameAndLLDBRegnum::iterator it = m_reg_map.begin();
       it != m_reg_map.end(); ++it) {
    for (size_t i = 0; i < reg_info.size(); ++i) {
      if (::strcmp(reg_info[i].name, it->second.name) == 0) {
        it->second.lldb_regnum = reg_info[i].lldb_regnum;
        break;
      }
    }
  }

  uint32_t lldb_regno;
  if (machine_regno_to_lldb_regno(m_machine_sp_regnum, lldb_regno))
    m_lldb_sp_regnum = lldb_regno;
  if (machine_regno_to_lldb_regno(m_machine_fp_regnum, lldb_regno))
    m_lldb_fp_regnum = lldb_regno;
  if (machine_regno_to_lldb_regno(m_machine_alt_fp_regnum, lldb_regno))
    m_lldb_alt_fp_regnum = lldb_regno;
  if (machine_regno_to_lldb_regno(m_machine_ip_regnum, lldb_regno))
    m_lldb_ip_regnum = lldb_regno;

  m_register_map_initialized = true;
}

// This function expects an x86 native register number (i.e. the bits stripped
// out of the actual instruction), not an lldb register number.
//
// FIXME: This is ABI dependent, it shouldn't be hardcoded here.

bool x86AssemblyInspectionEngine::nonvolatile_reg_p(int machine_regno) {
  if (m_cpu == k_i386) {
    switch (machine_regno) {
    case k_machine_ebx:
    case k_machine_ebp: // not actually a nonvolatile but often treated as such
                        // by convention
    case k_machine_esi:
    case k_machine_edi:
    case k_machine_esp:
      return true;
    default:
      return false;
    }
  }
  if (m_cpu == k_x86_64) {
    switch (machine_regno) {
    case k_machine_rbx:
    case k_machine_rsp:
    case k_machine_rbp: // not actually a nonvolatile but often treated as such
                        // by convention
    case k_machine_r12:
    case k_machine_r13:
    case k_machine_r14:
    case k_machine_r15:
      return true;
    default:
      return false;
    }
  }
  return false;
}

// Macro to detect if this is a REX mode prefix byte.
#define REX_W_PREFIX_P(opcode) (((opcode) & (~0x5)) == 0x48)

// The high bit which should be added to the source register number (the "R"
// bit)
#define REX_W_SRCREG(opcode) (((opcode)&0x4) >> 2)

// The high bit which should be added to the destination register number (the
// "B" bit)
#define REX_W_DSTREG(opcode) ((opcode)&0x1)

// pushq %rbp [0x55]
bool x86AssemblyInspectionEngine::push_rbp_pattern_p() {
  uint8_t *p = m_cur_insn;
  return *p == 0x55;
}

// pushq $0 ; the first instruction in start() [0x6a 0x00]
bool x86AssemblyInspectionEngine::push_0_pattern_p() {
  uint8_t *p = m_cur_insn;
  return *p == 0x6a && *(p + 1) == 0x0;
}

// pushq $0
// pushl $0
bool x86AssemblyInspectionEngine::push_imm_pattern_p() {
  uint8_t *p = m_cur_insn;
  return *p == 0x68 || *p == 0x6a;
}

// pushl imm8(%esp)
//
// e.g. 0xff 0x74 0x24 0x20 - 'pushl 0x20(%esp)' (same byte pattern for 'pushq
// 0x20(%rsp)' in an x86_64 program)
//
// 0xff (with opcode bits '6' in next byte, PUSH r/m32) 0x74 (ModR/M byte with
// three bits used to specify the opcode)
//      mod == b01, opcode == b110, R/M == b100
//      "+disp8"
// 0x24 (SIB byte - scaled index = 0, r32 == esp) 0x20 imm8 value

bool x86AssemblyInspectionEngine::push_extended_pattern_p() {
  if (*m_cur_insn == 0xff) {
    // Get the 3 opcode bits from the ModR/M byte
    uint8_t opcode = (*(m_cur_insn + 1) >> 3) & 7;
    if (opcode == 6) {
      // I'm only looking for 0xff /6 here - I
      // don't really care what value is being pushed, just that we're pushing
      // a 32/64 bit value on to the stack is enough.
      return true;
    }
  }
  return false;
}

// instructions only valid in 32-bit mode:
// 0x0e - push cs
// 0x16 - push ss
// 0x1e - push ds
// 0x06 - push es
bool x86AssemblyInspectionEngine::push_misc_reg_p() {
  uint8_t p = *m_cur_insn;
  if (m_wordsize == 4) {
    if (p == 0x0e || p == 0x16 || p == 0x1e || p == 0x06)
      return true;
  }
  return false;
}

// pushq %rbx
// pushl %ebx
bool x86AssemblyInspectionEngine::push_reg_p(int &regno) {
  uint8_t *p = m_cur_insn;
  int regno_prefix_bit = 0;
  // If we have a rex prefix byte, check to see if a B bit is set
  if (m_wordsize == 8 && *p == 0x41) {
    regno_prefix_bit = 1 << 3;
    p++;
  }
  if (*p >= 0x50 && *p <= 0x57) {
    regno = (*p - 0x50) | regno_prefix_bit;
    return true;
  }
  return false;
}

// movq %rsp, %rbp [0x48 0x8b 0xec] or [0x48 0x89 0xe5] movl %esp, %ebp [0x8b
// 0xec] or [0x89 0xe5]
bool x86AssemblyInspectionEngine::mov_rsp_rbp_pattern_p() {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;
  if (*(p) == 0x8b && *(p + 1) == 0xec)
    return true;
  if (*(p) == 0x89 && *(p + 1) == 0xe5)
    return true;
  return false;
}

// movq %rsp, %rbx [0x48 0x8b 0xdc] or [0x48 0x89 0xe3]
// movl %esp, %ebx [0x8b 0xdc] or [0x89 0xe3]
bool x86AssemblyInspectionEngine::mov_rsp_rbx_pattern_p() {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;
  if (*(p) == 0x8b && *(p + 1) == 0xdc)
    return true;
  if (*(p) == 0x89 && *(p + 1) == 0xe3)
    return true;
  return false;
}

// movq %rbp, %rsp [0x48 0x8b 0xe5] or [0x48 0x89 0xec]
// movl %ebp, %esp [0x8b 0xe5] or [0x89 0xec]
bool x86AssemblyInspectionEngine::mov_rbp_rsp_pattern_p() {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;
  if (*(p) == 0x8b && *(p + 1) == 0xe5)
    return true;
  if (*(p) == 0x89 && *(p + 1) == 0xec)
    return true;
  return false;
}

// movq %rbx, %rsp [0x48 0x8b 0xe3] or [0x48 0x89 0xdc]
// movl %ebx, %esp [0x8b 0xe3] or [0x89 0xdc]
bool x86AssemblyInspectionEngine::mov_rbx_rsp_pattern_p() {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;
  if (*(p) == 0x8b && *(p + 1) == 0xe3)
    return true;
  if (*(p) == 0x89 && *(p + 1) == 0xdc)
    return true;
  return false;
}

// subq $0x20, %rsp
bool x86AssemblyInspectionEngine::sub_rsp_pattern_p(int &amount) {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;
  // 8-bit immediate operand
  if (*p == 0x83 && *(p + 1) == 0xec) {
    amount = (int8_t) * (p + 2);
    return true;
  }
  // 32-bit immediate operand
  if (*p == 0x81 && *(p + 1) == 0xec) {
    amount = (int32_t)extract_4(p + 2);
    return true;
  }
  return false;
}

// addq $0x20, %rsp
bool x86AssemblyInspectionEngine::add_rsp_pattern_p(int &amount) {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;
  // 8-bit immediate operand
  if (*p == 0x83 && *(p + 1) == 0xc4) {
    amount = (int8_t) * (p + 2);
    return true;
  }
  // 32-bit immediate operand
  if (*p == 0x81 && *(p + 1) == 0xc4) {
    amount = (int32_t)extract_4(p + 2);
    return true;
  }
  return false;
}

// lea esp, [esp - 0x28]
// lea esp, [esp + 0x28]
bool x86AssemblyInspectionEngine::lea_rsp_pattern_p(int &amount) {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;

  // Check opcode
  if (*p != 0x8d)
    return false;

  // 8 bit displacement
  if (*(p + 1) == 0x64 && (*(p + 2) & 0x3f) == 0x24) {
    amount = (int8_t) * (p + 3);
    return true;
  }

  // 32 bit displacement
  if (*(p + 1) == 0xa4 && (*(p + 2) & 0x3f) == 0x24) {
    amount = (int32_t)extract_4(p + 3);
    return true;
  }

  return false;
}

// lea -0x28(%ebp), %esp
// (32-bit and 64-bit variants, 8-bit and 32-bit displacement)
bool x86AssemblyInspectionEngine::lea_rbp_rsp_pattern_p(int &amount) {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;

  // Check opcode
  if (*p != 0x8d)
    return false;
  ++p;

  // 8 bit displacement
  if (*p == 0x65) {
    amount = (int8_t)p[1];
    return true;
  }

  // 32 bit displacement
  if (*p == 0xa5) {
    amount = (int32_t)extract_4(p + 1);
    return true;
  }

  return false;
}

// lea -0x28(%ebx), %esp
// (32-bit and 64-bit variants, 8-bit and 32-bit displacement)
bool x86AssemblyInspectionEngine::lea_rbx_rsp_pattern_p(int &amount) {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;

  // Check opcode
  if (*p != 0x8d)
    return false;
  ++p;

  // 8 bit displacement
  if (*p == 0x63) {
    amount = (int8_t)p[1];
    return true;
  }

  // 32 bit displacement
  if (*p == 0xa3) {
    amount = (int32_t)extract_4(p + 1);
    return true;
  }

  return false;
}

// and -0xfffffff0, %esp
// (32-bit and 64-bit variants, 8-bit and 32-bit displacement)
bool x86AssemblyInspectionEngine::and_rsp_pattern_p() {
  uint8_t *p = m_cur_insn;
  if (m_wordsize == 8 && *p == 0x48)
    p++;

  if (*p != 0x81 && *p != 0x83)
    return false;

  return *++p == 0xe4;
}

// popq %rbx
// popl %ebx
bool x86AssemblyInspectionEngine::pop_reg_p(int &regno) {
  uint8_t *p = m_cur_insn;
  int regno_prefix_bit = 0;
  // If we have a rex prefix byte, check to see if a B bit is set
  if (m_wordsize == 8 && *p == 0x41) {
    regno_prefix_bit = 1 << 3;
    p++;
  }
  if (*p >= 0x58 && *p <= 0x5f) {
    regno = (*p - 0x58) | regno_prefix_bit;
    return true;
  }
  return false;
}

// popq %rbp [0x5d]
// popl %ebp [0x5d]
bool x86AssemblyInspectionEngine::pop_rbp_pattern_p() {
  uint8_t *p = m_cur_insn;
  return (*p == 0x5d);
}

// instructions valid only in 32-bit mode:
// 0x1f - pop ds
// 0x07 - pop es
// 0x17 - pop ss
bool x86AssemblyInspectionEngine::pop_misc_reg_p() {
  uint8_t p = *m_cur_insn;
  if (m_wordsize == 4) {
    if (p == 0x1f || p == 0x07 || p == 0x17)
      return true;
  }
  return false;
}

// leave [0xc9]
bool x86AssemblyInspectionEngine::leave_pattern_p() {
  uint8_t *p = m_cur_insn;
  return (*p == 0xc9);
}

// call $0 [0xe8 0x0 0x0 0x0 0x0]
bool x86AssemblyInspectionEngine::call_next_insn_pattern_p() {
  uint8_t *p = m_cur_insn;
  return (*p == 0xe8) && (*(p + 1) == 0x0) && (*(p + 2) == 0x0) &&
         (*(p + 3) == 0x0) && (*(p + 4) == 0x0);
}

// Look for an instruction sequence storing a nonvolatile register on to the
// stack frame.

//  movq %rax, -0x10(%rbp) [0x48 0x89 0x45 0xf0]
//  movl %eax, -0xc(%ebp)  [0x89 0x45 0xf4]

// The offset value returned in rbp_offset will be positive -- but it must be
// subtraced from the frame base register to get the actual location.  The
// positive value returned for the offset is a convention used elsewhere for
// CFA offsets et al.

bool x86AssemblyInspectionEngine::mov_reg_to_local_stack_frame_p(
    int &regno, int &rbp_offset) {
  uint8_t *p = m_cur_insn;
  int src_reg_prefix_bit = 0;
  int target_reg_prefix_bit = 0;

  if (m_wordsize == 8 && REX_W_PREFIX_P(*p)) {
    src_reg_prefix_bit = REX_W_SRCREG(*p) << 3;
    target_reg_prefix_bit = REX_W_DSTREG(*p) << 3;
    if (target_reg_prefix_bit == 1) {
      // rbp/ebp don't need a prefix bit - we know this isn't the reg we care
      // about.
      return false;
    }
    p++;
  }

  if (*p == 0x89) {
    /* Mask off the 3-5 bits which indicate the destination register
       if this is a ModR/M byte.  */
    int opcode_destreg_masked_out = *(p + 1) & (~0x38);

    /* Is this a ModR/M byte with Mod bits 01 and R/M bits 101
       and three bits between them, e.g. 01nnn101
       We're looking for a destination of ebp-disp8 or ebp-disp32.   */
    int immsize;
    if (opcode_destreg_masked_out == 0x45)
      immsize = 2;
    else if (opcode_destreg_masked_out == 0x85)
      immsize = 4;
    else
      return false;

    int offset = 0;
    if (immsize == 2)
      offset = (int8_t) * (p + 2);
    if (immsize == 4)
      offset = (uint32_t)extract_4(p + 2);
    if (offset > 0)
      return false;

    regno = ((*(p + 1) >> 3) & 0x7) | src_reg_prefix_bit;
    rbp_offset = offset > 0 ? offset : -offset;
    return true;
  }
  return false;
}

// ret [0xc9] or [0xc2 imm8] or [0xca imm8]
bool x86AssemblyInspectionEngine::ret_pattern_p() {
  uint8_t *p = m_cur_insn;
  return *p == 0xc9 || *p == 0xc2 || *p == 0xca || *p == 0xc3;
}

uint32_t x86AssemblyInspectionEngine::extract_4(uint8_t *b) {
  uint32_t v = 0;
  for (int i = 3; i >= 0; i--)
    v = (v << 8) | b[i];
  return v;
}

bool x86AssemblyInspectionEngine::instruction_length(uint8_t *insn_p,
                                                     int &length, 
                                                     uint32_t buffer_remaining_bytes) {

  uint32_t max_op_byte_size = std::min(buffer_remaining_bytes, m_arch.GetMaximumOpcodeByteSize());
  llvm::SmallVector<uint8_t, 32> opcode_data;
  opcode_data.resize(max_op_byte_size);

  char out_string[512];
  const size_t inst_size =
      ::LLVMDisasmInstruction(m_disasm_context, insn_p, max_op_byte_size, 0,
                              out_string, sizeof(out_string));

  length = inst_size;
  return true;
}

bool x86AssemblyInspectionEngine::machine_regno_to_lldb_regno(
    int machine_regno, uint32_t &lldb_regno) {
  MachineRegnumToNameAndLLDBRegnum::iterator it = m_reg_map.find(machine_regno);
  if (it != m_reg_map.end()) {
    lldb_regno = it->second.lldb_regnum;
    return true;
  }
  return false;
  return false;
}

bool x86AssemblyInspectionEngine::GetNonCallSiteUnwindPlanFromAssembly(
    uint8_t *data, size_t size, AddressRange &func_range,
    UnwindPlan &unwind_plan) {
  unwind_plan.Clear();

  if (data == nullptr || size == 0)
    return false;

  if (!m_register_map_initialized)
    return false;

  addr_t current_func_text_offset = 0;
  int current_sp_bytes_offset_from_fa = 0;
  bool is_aligned = false;
  UnwindPlan::Row::RegisterLocation initial_regloc;
  UnwindPlan::RowSP row(new UnwindPlan::Row);

  unwind_plan.SetPlanValidAddressRange(func_range);
  unwind_plan.SetRegisterKind(eRegisterKindLLDB);

  // At the start of the function, find the CFA by adding wordsize to the SP
  // register
  row->SetOffset(current_func_text_offset);
  row->GetCFAValue().SetIsRegisterPlusOffset(m_lldb_sp_regnum, m_wordsize);

  // caller's stack pointer value before the call insn is the CFA address
  initial_regloc.SetIsCFAPlusOffset(0);
  row->SetRegisterInfo(m_lldb_sp_regnum, initial_regloc);

  // saved instruction pointer can be found at CFA - wordsize.
  current_sp_bytes_offset_from_fa = m_wordsize;
  initial_regloc.SetAtCFAPlusOffset(-current_sp_bytes_offset_from_fa);
  row->SetRegisterInfo(m_lldb_ip_regnum, initial_regloc);

  unwind_plan.AppendRow(row);

  // Allocate a new Row, populate it with the existing Row contents.
  UnwindPlan::Row *newrow = new UnwindPlan::Row;
  *newrow = *row.get();
  row.reset(newrow);

  // Track which registers have been saved so far in the prologue. If we see
  // another push of that register, it's not part of the prologue. The register
  // numbers used here are the machine register #'s (i386_register_numbers,
  // x86_64_register_numbers).
  std::vector<bool> saved_registers(32, false);

  // Once the prologue has completed we'll save a copy of the unwind
  // instructions If there is an epilogue in the middle of the function, after
  // that epilogue we'll reinstate the unwind setup -- we assume that some code
  // path jumps over the mid-function epilogue

  UnwindPlan::RowSP prologue_completed_row; // copy of prologue row of CFI
  int prologue_completed_sp_bytes_offset_from_cfa; // The sp value before the
                                                   // epilogue started executed
  bool prologue_completed_is_aligned;
  std::vector<bool> prologue_completed_saved_registers;

  while (current_func_text_offset < size) {
    int stack_offset, insn_len;
    int machine_regno;   // register numbers masked directly out of instructions
    uint32_t lldb_regno; // register numbers in lldb's eRegisterKindLLDB
                         // numbering scheme

    bool in_epilogue = false; // we're in the middle of an epilogue sequence
    bool row_updated = false; // The UnwindPlan::Row 'row' has been updated

    m_cur_insn = data + current_func_text_offset;
    if (!instruction_length(m_cur_insn, insn_len, size - current_func_text_offset)
        || insn_len == 0 
        || insn_len > kMaxInstructionByteSize) {
      // An unrecognized/junk instruction
      break;
    }

    auto &cfa_value = row->GetCFAValue();
    auto &afa_value = row->GetAFAValue();
    auto fa_value_ptr = is_aligned ? &afa_value : &cfa_value;

    if (mov_rsp_rbp_pattern_p()) {
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetIsRegisterPlusOffset(
            m_lldb_fp_regnum, fa_value_ptr->GetOffset());
        row_updated = true;
      }
    }

    else if (mov_rsp_rbx_pattern_p()) {
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetIsRegisterPlusOffset(
            m_lldb_alt_fp_regnum, fa_value_ptr->GetOffset());
        row_updated = true;
      }
    }

    else if (and_rsp_pattern_p()) {
      current_sp_bytes_offset_from_fa = 0;
      afa_value.SetIsRegisterPlusOffset(
          m_lldb_sp_regnum, current_sp_bytes_offset_from_fa);
      fa_value_ptr = &afa_value;
      is_aligned = true;
      row_updated = true;
    }

    else if (mov_rbp_rsp_pattern_p()) {
      if (is_aligned && cfa_value.GetRegisterNumber() == m_lldb_fp_regnum)
      {
        is_aligned = false;
        fa_value_ptr = &cfa_value;
        afa_value.SetUnspecified();
        row_updated = true;
      }
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_fp_regnum)
        current_sp_bytes_offset_from_fa = fa_value_ptr->GetOffset();
    }

    else if (mov_rbx_rsp_pattern_p()) {
      if (is_aligned && cfa_value.GetRegisterNumber() == m_lldb_alt_fp_regnum)
      {
        is_aligned = false;
        fa_value_ptr = &cfa_value;
        afa_value.SetUnspecified();
        row_updated = true;
      }
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_alt_fp_regnum)
        current_sp_bytes_offset_from_fa = fa_value_ptr->GetOffset();
    }

    // This is the start() function (or a pthread equivalent), it starts with a
    // pushl $0x0 which puts the saved pc value of 0 on the stack.  In this
    // case we want to pretend we didn't see a stack movement at all --
    // normally the saved pc value is already on the stack by the time the
    // function starts executing.
    else if (push_0_pattern_p()) {
    }

    else if (push_reg_p(machine_regno)) {
      current_sp_bytes_offset_from_fa += m_wordsize;
      // the PUSH instruction has moved the stack pointer - if the FA is set
      // in terms of the stack pointer, we need to add a new row of
      // instructions.
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetOffset(current_sp_bytes_offset_from_fa);
        row_updated = true;
      }
      // record where non-volatile (callee-saved, spilled) registers are saved
      // on the stack
      if (nonvolatile_reg_p(machine_regno) &&
          machine_regno_to_lldb_regno(machine_regno, lldb_regno) &&
          !saved_registers[machine_regno]) {
        UnwindPlan::Row::RegisterLocation regloc;
        if (is_aligned)
            regloc.SetAtAFAPlusOffset(-current_sp_bytes_offset_from_fa);
        else
            regloc.SetAtCFAPlusOffset(-current_sp_bytes_offset_from_fa);
        row->SetRegisterInfo(lldb_regno, regloc);
        saved_registers[machine_regno] = true;
        row_updated = true;
      }
    }

    else if (pop_reg_p(machine_regno)) {
      current_sp_bytes_offset_from_fa -= m_wordsize;

      if (nonvolatile_reg_p(machine_regno) &&
          machine_regno_to_lldb_regno(machine_regno, lldb_regno) &&
          saved_registers[machine_regno]) {
        saved_registers[machine_regno] = false;
        row->RemoveRegisterInfo(lldb_regno);

        if (lldb_regno == fa_value_ptr->GetRegisterNumber()) {
          fa_value_ptr->SetIsRegisterPlusOffset(
              m_lldb_sp_regnum, fa_value_ptr->GetOffset());
        }

        in_epilogue = true;
        row_updated = true;
      }

      // the POP instruction has moved the stack pointer - if the FA is set in
      // terms of the stack pointer, we need to add a new row of instructions.
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetIsRegisterPlusOffset(
            m_lldb_sp_regnum, current_sp_bytes_offset_from_fa);
        row_updated = true;
      }
    }

    else if (pop_misc_reg_p()) {
      current_sp_bytes_offset_from_fa -= m_wordsize;
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetIsRegisterPlusOffset(
            m_lldb_sp_regnum, current_sp_bytes_offset_from_fa);
        row_updated = true;
      }
    }

    // The LEAVE instruction moves the value from rbp into rsp and pops a value
    // off the stack into rbp (restoring the caller's rbp value). It is the
    // opposite of ENTER, or 'push rbp, mov rsp rbp'.
    else if (leave_pattern_p()) {
      if (saved_registers[m_machine_fp_regnum]) {
        saved_registers[m_machine_fp_regnum] = false;
        row->RemoveRegisterInfo(m_lldb_fp_regnum);

        row_updated = true;
      }

      if (is_aligned && cfa_value.GetRegisterNumber() == m_lldb_fp_regnum)
      {
        is_aligned = false;
        fa_value_ptr = &cfa_value;
        afa_value.SetUnspecified();
        row_updated = true;
      }

      if (fa_value_ptr->GetRegisterNumber() == m_lldb_fp_regnum)
      {
        fa_value_ptr->SetIsRegisterPlusOffset(
            m_lldb_sp_regnum, fa_value_ptr->GetOffset());

        current_sp_bytes_offset_from_fa = fa_value_ptr->GetOffset();
      }

      current_sp_bytes_offset_from_fa -= m_wordsize;

      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetIsRegisterPlusOffset(
            m_lldb_sp_regnum, current_sp_bytes_offset_from_fa);
        row_updated = true;
      }

      in_epilogue = true;
    }

    else if (mov_reg_to_local_stack_frame_p(machine_regno, stack_offset) &&
             nonvolatile_reg_p(machine_regno) &&
             machine_regno_to_lldb_regno(machine_regno, lldb_regno) &&
             !saved_registers[machine_regno]) {
      saved_registers[machine_regno] = true;

      UnwindPlan::Row::RegisterLocation regloc;

      // stack_offset for 'movq %r15, -80(%rbp)' will be 80. In the Row, we
      // want to express this as the offset from the FA.  If the frame base is
      // rbp (like the above instruction), the FA offset for rbp is probably
      // 16.  So we want to say that the value is stored at the FA address -
      // 96.
      if (is_aligned)
          regloc.SetAtAFAPlusOffset(-(stack_offset + fa_value_ptr->GetOffset()));
      else
          regloc.SetAtCFAPlusOffset(-(stack_offset + fa_value_ptr->GetOffset()));

      row->SetRegisterInfo(lldb_regno, regloc);

      row_updated = true;
    }

    else if (sub_rsp_pattern_p(stack_offset)) {
      current_sp_bytes_offset_from_fa += stack_offset;
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetOffset(current_sp_bytes_offset_from_fa);
        row_updated = true;
      }
    }

    else if (add_rsp_pattern_p(stack_offset)) {
      current_sp_bytes_offset_from_fa -= stack_offset;
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetOffset(current_sp_bytes_offset_from_fa);
        row_updated = true;
      }
      in_epilogue = true;
    }

    else if (push_extended_pattern_p() || push_imm_pattern_p() ||
             push_misc_reg_p()) {
      current_sp_bytes_offset_from_fa += m_wordsize;
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetOffset(current_sp_bytes_offset_from_fa);
        row_updated = true;
      }
    }

    else if (lea_rsp_pattern_p(stack_offset)) {
      current_sp_bytes_offset_from_fa -= stack_offset;
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetOffset(current_sp_bytes_offset_from_fa);
        row_updated = true;
      }
      if (stack_offset > 0)
        in_epilogue = true;
    }

    else if (lea_rbp_rsp_pattern_p(stack_offset)) {
      if (is_aligned &&
          cfa_value.GetRegisterNumber() == m_lldb_fp_regnum) {
        is_aligned = false;
        fa_value_ptr = &cfa_value;
        afa_value.SetUnspecified();
        row_updated = true;
      }
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_fp_regnum) {
        current_sp_bytes_offset_from_fa =
          fa_value_ptr->GetOffset() - stack_offset;
      }
    }

    else if (lea_rbx_rsp_pattern_p(stack_offset)) {
      if (is_aligned &&
          cfa_value.GetRegisterNumber() == m_lldb_alt_fp_regnum) {
        is_aligned = false;
        fa_value_ptr = &cfa_value;
        afa_value.SetUnspecified();
        row_updated = true;
      }
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_alt_fp_regnum) {
        current_sp_bytes_offset_from_fa = fa_value_ptr->GetOffset() - stack_offset;
      }
    }

    else if (ret_pattern_p() && prologue_completed_row.get()) {
      // Reinstate the saved prologue setup for any instructions that come
      // after the ret instruction

      UnwindPlan::Row *newrow = new UnwindPlan::Row;
      *newrow = *prologue_completed_row.get();
      row.reset(newrow);
      current_sp_bytes_offset_from_fa =
          prologue_completed_sp_bytes_offset_from_cfa;
      is_aligned = prologue_completed_is_aligned;

      saved_registers.clear();
      saved_registers.resize(prologue_completed_saved_registers.size(), false);
      for (size_t i = 0; i < prologue_completed_saved_registers.size(); ++i) {
        saved_registers[i] = prologue_completed_saved_registers[i];
      }

      in_epilogue = true;
      row_updated = true;
    }

    // call next instruction
    //     call 0
    //  => pop  %ebx
    // This is used in i386 programs to get the PIC base address for finding
    // global data
    else if (call_next_insn_pattern_p()) {
      current_sp_bytes_offset_from_fa += m_wordsize;
      if (fa_value_ptr->GetRegisterNumber() == m_lldb_sp_regnum) {
        fa_value_ptr->SetOffset(current_sp_bytes_offset_from_fa);
        row_updated = true;
      }
    }

    if (row_updated) {
      if (current_func_text_offset + insn_len < size) {
        row->SetOffset(current_func_text_offset + insn_len);
        unwind_plan.AppendRow(row);
        // Allocate a new Row, populate it with the existing Row contents.
        newrow = new UnwindPlan::Row;
        *newrow = *row.get();
        row.reset(newrow);
      }
    }

    if (!in_epilogue && row_updated) {
      // If we're not in an epilogue sequence, save the updated Row
      UnwindPlan::Row *newrow = new UnwindPlan::Row;
      *newrow = *row.get();
      prologue_completed_row.reset(newrow);

      prologue_completed_saved_registers.clear();
      prologue_completed_saved_registers.resize(saved_registers.size(), false);
      for (size_t i = 0; i < saved_registers.size(); ++i) {
        prologue_completed_saved_registers[i] = saved_registers[i];
      }
    }

    // We may change the sp value without adding a new Row necessarily -- keep
    // track of it either way.
    if (!in_epilogue) {
      prologue_completed_sp_bytes_offset_from_cfa =
          current_sp_bytes_offset_from_fa;
      prologue_completed_is_aligned = is_aligned;
    }

    m_cur_insn = m_cur_insn + insn_len;
    current_func_text_offset += insn_len;
  }

  unwind_plan.SetSourceName("assembly insn profiling");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);

  return true;
}

bool x86AssemblyInspectionEngine::AugmentUnwindPlanFromCallSite(
    uint8_t *data, size_t size, AddressRange &func_range,
    UnwindPlan &unwind_plan, RegisterContextSP &reg_ctx) {
  Address addr_start = func_range.GetBaseAddress();
  if (!addr_start.IsValid())
    return false;

  // We either need a live RegisterContext, or we need the UnwindPlan to
  // already be in the lldb register numbering scheme.
  if (reg_ctx.get() == nullptr &&
      unwind_plan.GetRegisterKind() != eRegisterKindLLDB)
    return false;

  // Is original unwind_plan valid?
  // unwind_plan should have at least one row which is ABI-default (CFA
  // register is sp), and another row in mid-function.
  if (unwind_plan.GetRowCount() < 2)
    return false;

  UnwindPlan::RowSP first_row = unwind_plan.GetRowAtIndex(0);
  if (first_row->GetOffset() != 0)
    return false;
  uint32_t cfa_reg = first_row->GetCFAValue().GetRegisterNumber();
  if (unwind_plan.GetRegisterKind() != eRegisterKindLLDB) {
    cfa_reg = reg_ctx->ConvertRegisterKindToRegisterNumber(
        unwind_plan.GetRegisterKind(),
        first_row->GetCFAValue().GetRegisterNumber());
  }
  if (cfa_reg != m_lldb_sp_regnum ||
      first_row->GetCFAValue().GetOffset() != m_wordsize)
    return false;

  UnwindPlan::RowSP original_last_row = unwind_plan.GetRowForFunctionOffset(-1);

  size_t offset = 0;
  int row_id = 1;
  bool unwind_plan_updated = false;
  UnwindPlan::RowSP row(new UnwindPlan::Row(*first_row));
  m_cur_insn = data + offset;

  // After a mid-function epilogue we will need to re-insert the original
  // unwind rules so unwinds work for the remainder of the function.  These
  // aren't common with clang/gcc on x86 but it is possible.
  bool reinstate_unwind_state = false;

  while (offset < size) {
    m_cur_insn = data + offset;
    int insn_len;
    if (!instruction_length(m_cur_insn, insn_len, size - offset)
        || insn_len == 0 
        || insn_len > kMaxInstructionByteSize) {
      // An unrecognized/junk instruction.
      break;
    }

    // Advance offsets.
    offset += insn_len;
    m_cur_insn = data + offset;

    // offset is pointing beyond the bounds of the function; stop looping.
    if (offset >= size) 
      continue;

    if (reinstate_unwind_state) {
      UnwindPlan::RowSP new_row(new UnwindPlan::Row());
      *new_row = *original_last_row;
      new_row->SetOffset(offset);
      unwind_plan.AppendRow(new_row);
      row.reset(new UnwindPlan::Row());
      *row = *new_row;
      reinstate_unwind_state = false;
      unwind_plan_updated = true;
      continue;
    }

    // If we already have one row for this instruction, we can continue.
    while (row_id < unwind_plan.GetRowCount() &&
           unwind_plan.GetRowAtIndex(row_id)->GetOffset() <= offset) {
      row_id++;
    }
    UnwindPlan::RowSP original_row = unwind_plan.GetRowAtIndex(row_id - 1);
    if (original_row->GetOffset() == offset) {
      *row = *original_row;
      continue;
    }

    if (row_id == 0) {
      // If we are here, compiler didn't generate CFI for prologue. This won't
      // happen to GCC or clang. In this case, bail out directly.
      return false;
    }

    // Inspect the instruction to check if we need a new row for it.
    cfa_reg = row->GetCFAValue().GetRegisterNumber();
    if (unwind_plan.GetRegisterKind() != eRegisterKindLLDB) {
      cfa_reg = reg_ctx->ConvertRegisterKindToRegisterNumber(
          unwind_plan.GetRegisterKind(),
          row->GetCFAValue().GetRegisterNumber());
    }
    if (cfa_reg == m_lldb_sp_regnum) {
      // CFA register is sp.

      // call next instruction
      //     call 0
      //  => pop  %ebx
      if (call_next_insn_pattern_p()) {
        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(m_wordsize);

        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }

      // push/pop register
      int regno;
      if (push_reg_p(regno)) {
        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(m_wordsize);

        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }
      if (pop_reg_p(regno)) {
        // Technically, this might be a nonvolatile register recover in
        // epilogue. We should reset RegisterInfo for the register. But in
        // practice, previous rule for the register is still valid... So we
        // ignore this case.

        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(-m_wordsize);

        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }

      if (pop_misc_reg_p()) {
        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(-m_wordsize);

        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }

      // push imm
      if (push_imm_pattern_p()) {
        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(m_wordsize);
        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }

      // push extended
      if (push_extended_pattern_p() || push_misc_reg_p()) {
        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(m_wordsize);
        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }

      // add/sub %rsp/%esp
      int amount;
      if (add_rsp_pattern_p(amount)) {
        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(-amount);

        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }
      if (sub_rsp_pattern_p(amount)) {
        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(amount);

        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }

      // lea %rsp, [%rsp + $offset]
      if (lea_rsp_pattern_p(amount)) {
        row->SetOffset(offset);
        row->GetCFAValue().IncOffset(-amount);

        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        continue;
      }

      if (ret_pattern_p()) {
        reinstate_unwind_state = true;
        continue;
      }
    } else if (cfa_reg == m_lldb_fp_regnum) {
      // CFA register is fp.

      // The only case we care about is epilogue:
      //     [0x5d] pop %rbp/%ebp
      //  => [0xc3] ret
      if (pop_rbp_pattern_p() || leave_pattern_p()) {
        offset += 1;
        row->SetOffset(offset);
        row->GetCFAValue().SetIsRegisterPlusOffset(
            first_row->GetCFAValue().GetRegisterNumber(), m_wordsize);

        UnwindPlan::RowSP new_row(new UnwindPlan::Row(*row));
        unwind_plan.InsertRow(new_row);
        unwind_plan_updated = true;
        reinstate_unwind_state = true;
        continue;
      }
    } else {
      // CFA register is not sp or fp.

      // This must be hand-written assembly.
      // Just trust eh_frame and assume we have finished.
      break;
    }
  }

  unwind_plan.SetPlanValidAddressRange(func_range);
  if (unwind_plan_updated) {
    std::string unwind_plan_source(unwind_plan.GetSourceName().AsCString());
    unwind_plan_source += " plus augmentation from assembly parsing";
    unwind_plan.SetSourceName(unwind_plan_source.c_str());
    unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
    unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolYes);
  }
  return true;
}

bool x86AssemblyInspectionEngine::FindFirstNonPrologueInstruction(
    uint8_t *data, size_t size, size_t &offset) {
  offset = 0;

  if (!m_register_map_initialized)
    return false;

  while (offset < size) {
    int regno;
    int insn_len;
    int scratch;

    m_cur_insn = data + offset;
    if (!instruction_length(m_cur_insn, insn_len, size - offset) 
        || insn_len > kMaxInstructionByteSize 
        || insn_len == 0) {
      // An error parsing the instruction, i.e. probably data/garbage - stop
      // scanning
      break;
    }

    if (push_rbp_pattern_p() || mov_rsp_rbp_pattern_p() ||
        sub_rsp_pattern_p(scratch) || push_reg_p(regno) ||
        mov_reg_to_local_stack_frame_p(regno, scratch) ||
        (lea_rsp_pattern_p(scratch) && offset == 0)) {
      offset += insn_len;
      continue;
    }
    //
    // Unknown non-prologue instruction - stop scanning
    break;
  }

  return true;
}
