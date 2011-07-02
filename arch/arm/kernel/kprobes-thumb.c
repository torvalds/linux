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
