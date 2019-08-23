// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/probes/kprobes/actions-common.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * Some contents moved here from arch/arm/include/asm/kprobes-arm.c which is
 * Copyright (C) 2006, 2007 Motorola Inc.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <asm/opcodes.h>

#include "core.h"


static void __kprobes simulate_ldm1stm1(probes_opcode_t insn,
		struct arch_probes_insn *asi,
		struct pt_regs *regs)
{
	int rn = (insn >> 16) & 0xf;
	int lbit = insn & (1 << 20);
	int wbit = insn & (1 << 21);
	int ubit = insn & (1 << 23);
	int pbit = insn & (1 << 24);
	long *addr = (long *)regs->uregs[rn];
	int reg_bit_vector;
	int reg_count;

	reg_count = 0;
	reg_bit_vector = insn & 0xffff;
	while (reg_bit_vector) {
		reg_bit_vector &= (reg_bit_vector - 1);
		++reg_count;
	}

	if (!ubit)
		addr -= reg_count;
	addr += (!pbit == !ubit);

	reg_bit_vector = insn & 0xffff;
	while (reg_bit_vector) {
		int reg = __ffs(reg_bit_vector);
		reg_bit_vector &= (reg_bit_vector - 1);
		if (lbit)
			regs->uregs[reg] = *addr++;
		else
			*addr++ = regs->uregs[reg];
	}

	if (wbit) {
		if (!ubit)
			addr -= reg_count;
		addr -= (!pbit == !ubit);
		regs->uregs[rn] = (long)addr;
	}
}

static void __kprobes simulate_stm1_pc(probes_opcode_t insn,
	struct arch_probes_insn *asi,
	struct pt_regs *regs)
{
	unsigned long addr = regs->ARM_pc - 4;

	regs->ARM_pc = (long)addr + str_pc_offset;
	simulate_ldm1stm1(insn, asi, regs);
	regs->ARM_pc = (long)addr + 4;
}

static void __kprobes simulate_ldm1_pc(probes_opcode_t insn,
	struct arch_probes_insn *asi,
	struct pt_regs *regs)
{
	simulate_ldm1stm1(insn, asi, regs);
	load_write_pc(regs->ARM_pc, regs);
}

static void __kprobes
emulate_generic_r0_12_noflags(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	register void *rregs asm("r1") = regs;
	register void *rfn asm("lr") = asi->insn_fn;

	__asm__ __volatile__ (
		"stmdb	sp!, {%[regs], r11}	\n\t"
		"ldmia	%[regs], {r0-r12}	\n\t"
#if __LINUX_ARM_ARCH__ >= 6
		"blx	%[fn]			\n\t"
#else
		"str	%[fn], [sp, #-4]!	\n\t"
		"adr	lr, 1f			\n\t"
		"ldr	pc, [sp], #4		\n\t"
		"1:				\n\t"
#endif
		"ldr	lr, [sp], #4		\n\t" /* lr = regs */
		"stmia	lr, {r0-r12}		\n\t"
		"ldr	r11, [sp], #4		\n\t"
		: [regs] "=r" (rregs), [fn] "=r" (rfn)
		: "0" (rregs), "1" (rfn)
		: "r0", "r2", "r3", "r4", "r5", "r6", "r7",
		  "r8", "r9", "r10", "r12", "memory", "cc"
		);
}

static void __kprobes
emulate_generic_r2_14_noflags(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	emulate_generic_r0_12_noflags(insn, asi,
		(struct pt_regs *)(regs->uregs+2));
}

static void __kprobes
emulate_ldm_r3_15(probes_opcode_t insn,
	struct arch_probes_insn *asi, struct pt_regs *regs)
{
	emulate_generic_r0_12_noflags(insn, asi,
		(struct pt_regs *)(regs->uregs+3));
	load_write_pc(regs->ARM_pc, regs);
}

enum probes_insn __kprobes
kprobe_decode_ldmstm(probes_opcode_t insn, struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	probes_insn_handler_t *handler = 0;
	unsigned reglist = insn & 0xffff;
	int is_ldm = insn & 0x100000;
	int rn = (insn >> 16) & 0xf;

	if (rn <= 12 && (reglist & 0xe000) == 0) {
		/* Instruction only uses registers in the range R0..R12 */
		handler = emulate_generic_r0_12_noflags;

	} else if (rn >= 2 && (reglist & 0x8003) == 0) {
		/* Instruction only uses registers in the range R2..R14 */
		rn -= 2;
		reglist >>= 2;
		handler = emulate_generic_r2_14_noflags;

	} else if (rn >= 3 && (reglist & 0x0007) == 0) {
		/* Instruction only uses registers in the range R3..R15 */
		if (is_ldm && (reglist & 0x8000)) {
			rn -= 3;
			reglist >>= 3;
			handler = emulate_ldm_r3_15;
		}
	}

	if (handler) {
		/* We can emulate the instruction in (possibly) modified form */
		asi->insn[0] = __opcode_to_mem_arm((insn & 0xfff00000) |
						   (rn << 16) | reglist);
		asi->insn_handler = handler;
		return INSN_GOOD;
	}

	/* Fallback to slower simulation... */
	if (reglist & 0x8000)
		handler = is_ldm ? simulate_ldm1_pc : simulate_stm1_pc;
	else
		handler = simulate_ldm1stm1;
	asi->insn_handler = handler;
	return INSN_GOOD_NO_SLOT;
}

