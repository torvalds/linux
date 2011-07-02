/*
 * arch/arm/kernel/kprobes-thumb.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/kprobes.h>

#include "kprobes.h"


/*
 * True if current instruction is in an IT block.
 */
#define in_it_block(cpsr)	((cpsr & 0x06000c00) != 0x00000000)

/*
 * Return the condition code to check for the currently executing instruction.
 * This is in ITSTATE<7:4> which is in CPSR<15:12> but is only valid if
 * in_it_block returns true.
 */
#define current_cond(cpsr)	((cpsr >> 12) & 0xf)

static unsigned long __kprobes
t16_emulate_loregs(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long oldcpsr = regs->ARM_cpsr;
	unsigned long newcpsr;

	__asm__ __volatile__ (
		"msr	cpsr_fs, %[oldcpsr]	\n\t"
		"ldmia	%[regs], {r0-r7}	\n\t"
		"blx	%[fn]			\n\t"
		"stmia	%[regs], {r0-r7}	\n\t"
		"mrs	%[newcpsr], cpsr	\n\t"
		: [newcpsr] "=r" (newcpsr)
		: [oldcpsr] "r" (oldcpsr), [regs] "r" (regs),
		  [fn] "r" (p->ainsn.insn_fn)
		: "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
		  "lr", "memory", "cc"
		);

	return (oldcpsr & ~APSR_MASK) | (newcpsr & APSR_MASK);
}

static void __kprobes
t16_emulate_loregs_rwflags(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_cpsr = t16_emulate_loregs(p, regs);
}

static void __kprobes
t16_emulate_loregs_noitrwflags(struct kprobe *p, struct pt_regs *regs)
{
	unsigned long cpsr = t16_emulate_loregs(p, regs);
	if (!in_it_block(cpsr))
		regs->ARM_cpsr = cpsr;
}

static const union decode_item t16_table_1011[] = {
	/* Miscellaneous 16-bit instructions		    */

	/*
	 * If-Then, and hints
	 *				1011 1111 xxxx xxxx
	 */

	/* YIELD			1011 1111 0001 0000 */
	DECODE_OR	(0xffff, 0xbf10),
	/* SEV				1011 1111 0100 0000 */
	DECODE_EMULATE	(0xffff, 0xbf40, kprobe_emulate_none),
	/* NOP				1011 1111 0000 0000 */
	/* WFE				1011 1111 0010 0000 */
	/* WFI				1011 1111 0011 0000 */
	DECODE_SIMULATE	(0xffcf, 0xbf00, kprobe_simulate_nop),
	/* Unassigned hints		1011 1111 xxxx 0000 */
	DECODE_REJECT	(0xff0f, 0xbf00),

	DECODE_END
};

const union decode_item kprobe_decode_thumb16_table[] = {

	/*
	 * Shift (immediate), add, subtract, move, and compare
	 *				00xx xxxx xxxx xxxx
	 */

	/* CMP (immediate)		0010 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xf800, 0x2800, t16_emulate_loregs_rwflags),

	/* ADD (register)		0001 100x xxxx xxxx */
	/* SUB (register)		0001 101x xxxx xxxx */
	/* LSL (immediate)		0000 0xxx xxxx xxxx */
	/* LSR (immediate)		0000 1xxx xxxx xxxx */
	/* ASR (immediate)		0001 0xxx xxxx xxxx */
	/* ADD (immediate, Thumb)	0001 110x xxxx xxxx */
	/* SUB (immediate, Thumb)	0001 111x xxxx xxxx */
	/* MOV (immediate)		0010 0xxx xxxx xxxx */
	/* ADD (immediate, Thumb)	0011 0xxx xxxx xxxx */
	/* SUB (immediate, Thumb)	0011 1xxx xxxx xxxx */
	DECODE_EMULATE	(0xc000, 0x0000, t16_emulate_loregs_noitrwflags),

	/*
	 * 16-bit Thumb data-processing instructions
	 *				0100 00xx xxxx xxxx
	 */

	/* TST (register)		0100 0010 00xx xxxx */
	DECODE_EMULATE	(0xffc0, 0x4200, t16_emulate_loregs_rwflags),
	/* CMP (register)		0100 0010 10xx xxxx */
	/* CMN (register)		0100 0010 11xx xxxx */
	DECODE_EMULATE	(0xff80, 0x4280, t16_emulate_loregs_rwflags),
	/* AND (register)		0100 0000 00xx xxxx */
	/* EOR (register)		0100 0000 01xx xxxx */
	/* LSL (register)		0100 0000 10xx xxxx */
	/* LSR (register)		0100 0000 11xx xxxx */
	/* ASR (register)		0100 0001 00xx xxxx */
	/* ADC (register)		0100 0001 01xx xxxx */
	/* SBC (register)		0100 0001 10xx xxxx */
	/* ROR (register)		0100 0001 11xx xxxx */
	/* RSB (immediate)		0100 0010 01xx xxxx */
	/* ORR (register)		0100 0011 00xx xxxx */
	/* MUL				0100 0011 00xx xxxx */
	/* BIC (register)		0100 0011 10xx xxxx */
	/* MVN (register)		0100 0011 10xx xxxx */
	DECODE_EMULATE	(0xfc00, 0x4000, t16_emulate_loregs_noitrwflags),

	/*
	 * Miscellaneous 16-bit instructions
	 *				1011 xxxx xxxx xxxx
	 */
	DECODE_TABLE	(0xf000, 0xb000, t16_table_1011),

	DECODE_END
};

static unsigned long __kprobes thumb_check_cc(unsigned long cpsr)
{
	if (unlikely(in_it_block(cpsr)))
		return kprobe_condition_checks[current_cond(cpsr)](cpsr);
	return true;
}

static void __kprobes thumb16_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_pc += 2;
	p->ainsn.insn_handler(p, regs);
	regs->ARM_cpsr = it_advance(regs->ARM_cpsr);
}

static void __kprobes thumb32_singlestep(struct kprobe *p, struct pt_regs *regs)
{
	regs->ARM_pc += 4;
	p->ainsn.insn_handler(p, regs);
	regs->ARM_cpsr = it_advance(regs->ARM_cpsr);
}

enum kprobe_insn __kprobes
thumb16_kprobe_decode_insn(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	asi->insn_singlestep = thumb16_singlestep;
	asi->insn_check_cc = thumb_check_cc;
	return kprobe_decode_insn(insn, asi, kprobe_decode_thumb16_table, true);
}

enum kprobe_insn __kprobes
thumb32_kprobe_decode_insn(kprobe_opcode_t insn, struct arch_specific_insn *asi)
{
	asi->insn_singlestep = thumb32_singlestep;
	asi->insn_check_cc = thumb_check_cc;
	return INSN_REJECTED;
}
