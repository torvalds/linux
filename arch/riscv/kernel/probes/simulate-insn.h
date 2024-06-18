/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _RISCV_KERNEL_PROBES_SIMULATE_INSN_H
#define _RISCV_KERNEL_PROBES_SIMULATE_INSN_H

#include <asm/insn.h>

#define RISCV_INSN_REJECTED(name, code)					\
	do {								\
		if (riscv_insn_is_##name(code)) {			\
			return INSN_REJECTED;				\
		}							\
	} while (0)

#define RISCV_INSN_SET_SIMULATE(name, code)				\
	do {								\
		if (riscv_insn_is_##name(code)) {			\
			api->handler = simulate_##name;			\
			return INSN_GOOD_NO_SLOT;			\
		}							\
	} while (0)

bool simulate_auipc(u32 opcode, unsigned long addr, struct pt_regs *regs);
bool simulate_branch(u32 opcode, unsigned long addr, struct pt_regs *regs);
bool simulate_jal(u32 opcode, unsigned long addr, struct pt_regs *regs);
bool simulate_jalr(u32 opcode, unsigned long addr, struct pt_regs *regs);
bool simulate_c_j(u32 opcode, unsigned long addr, struct pt_regs *regs);
bool simulate_c_jr(u32 opcode, unsigned long addr, struct pt_regs *regs);
bool simulate_c_jalr(u32 opcode, unsigned long addr, struct pt_regs *regs);
bool simulate_c_bnez(u32 opcode, unsigned long addr, struct pt_regs *regs);
bool simulate_c_beqz(u32 opcode, unsigned long addr, struct pt_regs *regs);

#endif /* _RISCV_KERNEL_PROBES_SIMULATE_INSN_H */
