// SPDX-License-Identifier: GPL-2.0-only
/*
 * arch/arm/probes/decode.c
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * Some contents moved here from arch/arm/include/asm/kprobes-arm.c which is
 * Copyright (C) 2006, 2007 Motorola Inc.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <asm/system_info.h>
#include <asm/ptrace.h>
#include <linux/bug.h>

#include "decode.h"


#ifndef find_str_pc_offset

/*
 * For STR and STM instructions, an ARM core may choose to use either
 * a +8 or a +12 displacement from the current instruction's address.
 * Whichever value is chosen for a given core, it must be the same for
 * both instructions and may not change.  This function measures it.
 */

int str_pc_offset;

void __init find_str_pc_offset(void)
{
	int addr, scratch, ret;

	__asm__ (
		"sub	%[ret], pc, #4		\n\t"
		"str	pc, %[addr]		\n\t"
		"ldr	%[scr], %[addr]		\n\t"
		"sub	%[ret], %[scr], %[ret]	\n\t"
		: [ret] "=r" (ret), [scr] "=r" (scratch), [addr] "+m" (addr));

	str_pc_offset = ret;
}

#endif /* !find_str_pc_offset */


#ifndef test_load_write_pc_interworking

bool load_write_pc_interworks;

void __init test_load_write_pc_interworking(void)
{
	int arch = cpu_architecture();
	BUG_ON(arch == CPU_ARCH_UNKNOWN);
	load_write_pc_interworks = arch >= CPU_ARCH_ARMv5T;
}

#endif /* !test_load_write_pc_interworking */


#ifndef test_alu_write_pc_interworking

bool alu_write_pc_interworks;

void __init test_alu_write_pc_interworking(void)
{
	int arch = cpu_architecture();
	BUG_ON(arch == CPU_ARCH_UNKNOWN);
	alu_write_pc_interworks = arch >= CPU_ARCH_ARMv7;
}

#endif /* !test_alu_write_pc_interworking */


void __init arm_probes_decode_init(void)
{
	find_str_pc_offset();
	test_load_write_pc_interworking();
	test_alu_write_pc_interworking();
}


static unsigned long __kprobes __check_eq(unsigned long cpsr)
{
	return cpsr & PSR_Z_BIT;
}

static unsigned long __kprobes __check_ne(unsigned long cpsr)
{
	return (~cpsr) & PSR_Z_BIT;
}

static unsigned long __kprobes __check_cs(unsigned long cpsr)
{
	return cpsr & PSR_C_BIT;
}

static unsigned long __kprobes __check_cc(unsigned long cpsr)
{
	return (~cpsr) & PSR_C_BIT;
}

static unsigned long __kprobes __check_mi(unsigned long cpsr)
{
	return cpsr & PSR_N_BIT;
}

static unsigned long __kprobes __check_pl(unsigned long cpsr)
{
	return (~cpsr) & PSR_N_BIT;
}

static unsigned long __kprobes __check_vs(unsigned long cpsr)
{
	return cpsr & PSR_V_BIT;
}

static unsigned long __kprobes __check_vc(unsigned long cpsr)
{
	return (~cpsr) & PSR_V_BIT;
}

static unsigned long __kprobes __check_hi(unsigned long cpsr)
{
	cpsr &= ~(cpsr >> 1); /* PSR_C_BIT &= ~PSR_Z_BIT */
	return cpsr & PSR_C_BIT;
}

static unsigned long __kprobes __check_ls(unsigned long cpsr)
{
	cpsr &= ~(cpsr >> 1); /* PSR_C_BIT &= ~PSR_Z_BIT */
	return (~cpsr) & PSR_C_BIT;
}

static unsigned long __kprobes __check_ge(unsigned long cpsr)
{
	cpsr ^= (cpsr << 3); /* PSR_N_BIT ^= PSR_V_BIT */
	return (~cpsr) & PSR_N_BIT;
}

static unsigned long __kprobes __check_lt(unsigned long cpsr)
{
	cpsr ^= (cpsr << 3); /* PSR_N_BIT ^= PSR_V_BIT */
	return cpsr & PSR_N_BIT;
}

static unsigned long __kprobes __check_gt(unsigned long cpsr)
{
	unsigned long temp = cpsr ^ (cpsr << 3); /* PSR_N_BIT ^= PSR_V_BIT */
	temp |= (cpsr << 1);			 /* PSR_N_BIT |= PSR_Z_BIT */
	return (~temp) & PSR_N_BIT;
}

static unsigned long __kprobes __check_le(unsigned long cpsr)
{
	unsigned long temp = cpsr ^ (cpsr << 3); /* PSR_N_BIT ^= PSR_V_BIT */
	temp |= (cpsr << 1);			 /* PSR_N_BIT |= PSR_Z_BIT */
	return temp & PSR_N_BIT;
}

static unsigned long __kprobes __check_al(unsigned long cpsr)
{
	return true;
}

probes_check_cc * const probes_condition_checks[16] = {
	&__check_eq, &__check_ne, &__check_cs, &__check_cc,
	&__check_mi, &__check_pl, &__check_vs, &__check_vc,
	&__check_hi, &__check_ls, &__check_ge, &__check_lt,
	&__check_gt, &__check_le, &__check_al, &__check_al
};


void __kprobes probes_simulate_nop(probes_opcode_t opcode,
	struct arch_probes_insn *asi,
	struct pt_regs *regs)
{
}

void __kprobes probes_emulate_none(probes_opcode_t opcode,
	struct arch_probes_insn *asi,
	struct pt_regs *regs)
{
	asi->insn_fn();
}

/*
 * Prepare an instruction slot to receive an instruction for emulating.
 * This is done by placing a subroutine return after the location where the
 * instruction will be placed. We also modify ARM instructions to be
 * unconditional as the condition code will already be checked before any
 * emulation handler is called.
 */
static probes_opcode_t __kprobes
prepare_emulated_insn(probes_opcode_t insn, struct arch_probes_insn *asi,
		      bool thumb)
{
#ifdef CONFIG_THUMB2_KERNEL
	if (thumb) {
		u16 *thumb_insn = (u16 *)asi->insn;
		/* Thumb bx lr */
		thumb_insn[1] = __opcode_to_mem_thumb16(0x4770);
		thumb_insn[2] = __opcode_to_mem_thumb16(0x4770);
		return insn;
	}
	asi->insn[1] = __opcode_to_mem_arm(0xe12fff1e); /* ARM bx lr */
#else
	asi->insn[1] = __opcode_to_mem_arm(0xe1a0f00e); /* mov pc, lr */
#endif
	/* Make an ARM instruction unconditional */
	if (insn < 0xe0000000)
		insn = (insn | 0xe0000000) & ~0x10000000;
	return insn;
}

/*
 * Write a (probably modified) instruction into the slot previously prepared by
 * prepare_emulated_insn
 */
static void  __kprobes
set_emulated_insn(probes_opcode_t insn, struct arch_probes_insn *asi,
		  bool thumb)
{
#ifdef CONFIG_THUMB2_KERNEL
	if (thumb) {
		u16 *ip = (u16 *)asi->insn;
		if (is_wide_instruction(insn))
			*ip++ = __opcode_to_mem_thumb16(insn >> 16);
		*ip++ = __opcode_to_mem_thumb16(insn);
		return;
	}
#endif
	asi->insn[0] = __opcode_to_mem_arm(insn);
}

/*
 * When we modify the register numbers encoded in an instruction to be emulated,
 * the new values come from this define. For ARM and 32-bit Thumb instructions
 * this gives...
 *
 *	bit position	  16  12   8   4   0
 *	---------------+---+---+---+---+---+
 *	register	 r2  r0  r1  --  r3
 */
#define INSN_NEW_BITS		0x00020103

/* Each nibble has same value as that at INSN_NEW_BITS bit 16 */
#define INSN_SAMEAS16_BITS	0x22222222

/*
 * Validate and modify each of the registers encoded in an instruction.
 *
 * Each nibble in regs contains a value from enum decode_reg_type. For each
 * non-zero value, the corresponding nibble in pinsn is validated and modified
 * according to the type.
 */
static bool __kprobes decode_regs(probes_opcode_t *pinsn, u32 regs, bool modify)
{
	probes_opcode_t insn = *pinsn;
	probes_opcode_t mask = 0xf; /* Start at least significant nibble */

	for (; regs != 0; regs >>= 4, mask <<= 4) {

		probes_opcode_t new_bits = INSN_NEW_BITS;

		switch (regs & 0xf) {

		case REG_TYPE_NONE:
			/* Nibble not a register, skip to next */
			continue;

		case REG_TYPE_ANY:
			/* Any register is allowed */
			break;

		case REG_TYPE_SAMEAS16:
			/* Replace register with same as at bit position 16 */
			new_bits = INSN_SAMEAS16_BITS;
			break;

		case REG_TYPE_SP:
			/* Only allow SP (R13) */
			if ((insn ^ 0xdddddddd) & mask)
				goto reject;
			break;

		case REG_TYPE_PC:
			/* Only allow PC (R15) */
			if ((insn ^ 0xffffffff) & mask)
				goto reject;
			break;

		case REG_TYPE_NOSP:
			/* Reject SP (R13) */
			if (((insn ^ 0xdddddddd) & mask) == 0)
				goto reject;
			break;

		case REG_TYPE_NOSPPC:
		case REG_TYPE_NOSPPCX:
			/* Reject SP and PC (R13 and R15) */
			if (((insn ^ 0xdddddddd) & 0xdddddddd & mask) == 0)
				goto reject;
			break;

		case REG_TYPE_NOPCWB:
			if (!is_writeback(insn))
				break; /* No writeback, so any register is OK */
			fallthrough;
		case REG_TYPE_NOPC:
		case REG_TYPE_NOPCX:
			/* Reject PC (R15) */
			if (((insn ^ 0xffffffff) & mask) == 0)
				goto reject;
			break;
		}

		/* Replace value of nibble with new register number... */
		insn &= ~mask;
		insn |= new_bits & mask;
	}

	if (modify)
		*pinsn = insn;

	return true;

reject:
	return false;
}

static const int decode_struct_sizes[NUM_DECODE_TYPES] = {
	[DECODE_TYPE_TABLE]	= sizeof(struct decode_table),
	[DECODE_TYPE_CUSTOM]	= sizeof(struct decode_custom),
	[DECODE_TYPE_SIMULATE]	= sizeof(struct decode_simulate),
	[DECODE_TYPE_EMULATE]	= sizeof(struct decode_emulate),
	[DECODE_TYPE_OR]	= sizeof(struct decode_or),
	[DECODE_TYPE_REJECT]	= sizeof(struct decode_reject)
};

static int run_checkers(const struct decode_checker *checkers[],
		int action, probes_opcode_t insn,
		struct arch_probes_insn *asi,
		const struct decode_header *h)
{
	const struct decode_checker **p;

	if (!checkers)
		return INSN_GOOD;

	p = checkers;
	while (*p != NULL) {
		int retval;
		probes_check_t *checker_func = (*p)[action].checker;

		retval = INSN_GOOD;
		if (checker_func)
			retval = checker_func(insn, asi, h);
		if (retval == INSN_REJECTED)
			return retval;
		p++;
	}
	return INSN_GOOD;
}

/*
 * probes_decode_insn operates on data tables in order to decode an ARM
 * architecture instruction onto which a kprobe has been placed.
 *
 * These instruction decoding tables are a concatenation of entries each
 * of which consist of one of the following structs:
 *
 *	decode_table
 *	decode_custom
 *	decode_simulate
 *	decode_emulate
 *	decode_or
 *	decode_reject
 *
 * Each of these starts with a struct decode_header which has the following
 * fields:
 *
 *	type_regs
 *	mask
 *	value
 *
 * The least significant DECODE_TYPE_BITS of type_regs contains a value
 * from enum decode_type, this indicates which of the decode_* structs
 * the entry contains. The value DECODE_TYPE_END indicates the end of the
 * table.
 *
 * When the table is parsed, each entry is checked in turn to see if it
 * matches the instruction to be decoded using the test:
 *
 *	(insn & mask) == value
 *
 * If no match is found before the end of the table is reached then decoding
 * fails with INSN_REJECTED.
 *
 * When a match is found, decode_regs() is called to validate and modify each
 * of the registers encoded in the instruction; the data it uses to do this
 * is (type_regs >> DECODE_TYPE_BITS). A validation failure will cause decoding
 * to fail with INSN_REJECTED.
 *
 * Once the instruction has passed the above tests, further processing
 * depends on the type of the table entry's decode struct.
 *
 */
int __kprobes
probes_decode_insn(probes_opcode_t insn, struct arch_probes_insn *asi,
		   const union decode_item *table, bool thumb,
		   bool emulate, const union decode_action *actions,
		   const struct decode_checker *checkers[])
{
	const struct decode_header *h = (struct decode_header *)table;
	const struct decode_header *next;
	bool matched = false;
	/*
	 * @insn can be modified by decode_regs. Save its original
	 * value for checkers.
	 */
	probes_opcode_t origin_insn = insn;

	/*
	 * stack_space is initialized to 0 here. Checker functions
	 * should update is value if they find this is a stack store
	 * instruction: positive value means bytes of stack usage,
	 * negitive value means unable to determine stack usage
	 * statically. For instruction doesn't store to stack, checker
	 * do nothing with it.
	 */
	asi->stack_space = 0;

	/*
	 * Similarly to stack_space, register_usage_flags is filled by
	 * checkers. Its default value is set to ~0, which is 'all
	 * registers are used', to prevent any potential optimization.
	 */
	asi->register_usage_flags = ~0UL;

	if (emulate)
		insn = prepare_emulated_insn(insn, asi, thumb);

	for (;; h = next) {
		enum decode_type type = h->type_regs.bits & DECODE_TYPE_MASK;
		u32 regs = h->type_regs.bits >> DECODE_TYPE_BITS;

		if (type == DECODE_TYPE_END)
			return INSN_REJECTED;

		next = (struct decode_header *)
				((uintptr_t)h + decode_struct_sizes[type]);

		if (!matched && (insn & h->mask.bits) != h->value.bits)
			continue;

		if (!decode_regs(&insn, regs, emulate))
			return INSN_REJECTED;

		switch (type) {

		case DECODE_TYPE_TABLE: {
			struct decode_table *d = (struct decode_table *)h;
			next = (struct decode_header *)d->table.table;
			break;
		}

		case DECODE_TYPE_CUSTOM: {
			int err;
			struct decode_custom *d = (struct decode_custom *)h;
			int action = d->decoder.action;

			err = run_checkers(checkers, action, origin_insn, asi, h);
			if (err == INSN_REJECTED)
				return INSN_REJECTED;
			return actions[action].decoder(insn, asi, h);
		}

		case DECODE_TYPE_SIMULATE: {
			int err;
			struct decode_simulate *d = (struct decode_simulate *)h;
			int action = d->handler.action;

			err = run_checkers(checkers, action, origin_insn, asi, h);
			if (err == INSN_REJECTED)
				return INSN_REJECTED;
			asi->insn_handler = actions[action].handler;
			return INSN_GOOD_NO_SLOT;
		}

		case DECODE_TYPE_EMULATE: {
			int err;
			struct decode_emulate *d = (struct decode_emulate *)h;
			int action = d->handler.action;

			err = run_checkers(checkers, action, origin_insn, asi, h);
			if (err == INSN_REJECTED)
				return INSN_REJECTED;

			if (!emulate)
				return actions[action].decoder(insn, asi, h);

			asi->insn_handler = actions[action].handler;
			set_emulated_insn(insn, asi, thumb);
			return INSN_GOOD;
		}

		case DECODE_TYPE_OR:
			matched = true;
			break;

		case DECODE_TYPE_REJECT:
		default:
			return INSN_REJECTED;
		}
	}
}
