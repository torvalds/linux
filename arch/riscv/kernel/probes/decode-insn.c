// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <asm/sections.h>

#include "decode-insn.h"
#include "simulate-insn.h"

/* Return:
 *   INSN_REJECTED     If instruction is one not allowed to kprobe,
 *   INSN_GOOD_NO_SLOT If instruction is supported but doesn't use its slot.
 */
enum probe_insn __kprobes
riscv_probe_decode_insn(probe_opcode_t *addr, struct arch_probe_insn *api)
{
	probe_opcode_t insn = *addr;

	/*
	 * Reject instructions list:
	 */
	RISCV_INSN_REJECTED(system,		insn);
	RISCV_INSN_REJECTED(fence,		insn);

	/*
	 * Simulate instructions list:
	 * TODO: the REJECTED ones below need to be implemented
	 */
#ifdef CONFIG_RISCV_ISA_C
	RISCV_INSN_REJECTED(c_jal,		insn);
	RISCV_INSN_REJECTED(c_ebreak,		insn);

	RISCV_INSN_SET_SIMULATE(c_j,		insn);
	RISCV_INSN_SET_SIMULATE(c_jr,		insn);
	RISCV_INSN_SET_SIMULATE(c_jalr,		insn);
	RISCV_INSN_SET_SIMULATE(c_beqz,		insn);
	RISCV_INSN_SET_SIMULATE(c_bnez,		insn);
#endif

	RISCV_INSN_SET_SIMULATE(jal,		insn);
	RISCV_INSN_SET_SIMULATE(jalr,		insn);
	RISCV_INSN_SET_SIMULATE(auipc,		insn);
	RISCV_INSN_SET_SIMULATE(branch,		insn);

	return INSN_GOOD;
}
