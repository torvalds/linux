/*
 * arch/arm/probes/decode.h
 *
 * Copyright (C) 2011 Jon Medhurst <tixy@yxit.co.uk>.
 *
 * Some contents moved here from arch/arm/include/asm/kprobes.h which is
 * Copyright (C) 2006, 2007 Motorola Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef _ARM_KERNEL_PROBES_H
#define  _ARM_KERNEL_PROBES_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <asm/probes.h>

void __init arm_probes_decode_init(void);

extern probes_check_cc * const probes_condition_checks[16];

#if __LINUX_ARM_ARCH__ >= 7

/* str_pc_offset is architecturally defined from ARMv7 onwards */
#define str_pc_offset 8
#define find_str_pc_offset()

#else /* __LINUX_ARM_ARCH__ < 7 */

/* We need a run-time check to determine str_pc_offset */
extern int str_pc_offset;
void __init find_str_pc_offset(void);

#endif


/*
 * Update ITSTATE after normal execution of an IT block instruction.
 *
 * The 8 IT state bits are split into two parts in CPSR:
 *	ITSTATE<1:0> are in CPSR<26:25>
 *	ITSTATE<7:2> are in CPSR<15:10>
 */
static inline unsigned long it_advance(unsigned long cpsr)
	{
	if ((cpsr & 0x06000400) == 0) {
		/* ITSTATE<2:0> == 0 means end of IT block, so clear IT state */
		cpsr &= ~PSR_IT_MASK;
	} else {
		/* We need to shift left ITSTATE<4:0> */
		const unsigned long mask = 0x06001c00;  /* Mask ITSTATE<4:0> */
		unsigned long it = cpsr & mask;
		it <<= 1;
		it |= it >> (27 - 10);  /* Carry ITSTATE<2> to correct place */
		it &= mask;
		cpsr &= ~mask;
		cpsr |= it;
	}
	return cpsr;
}

static inline void __kprobes bx_write_pc(long pcv, struct pt_regs *regs)
{
	long cpsr = regs->ARM_cpsr;
	if (pcv & 0x1) {
		cpsr |= PSR_T_BIT;
		pcv &= ~0x1;
	} else {
		cpsr &= ~PSR_T_BIT;
		pcv &= ~0x2;	/* Avoid UNPREDICTABLE address allignment */
	}
	regs->ARM_cpsr = cpsr;
	regs->ARM_pc = pcv;
}


#if __LINUX_ARM_ARCH__ >= 6

/* Kernels built for >= ARMv6 should never run on <= ARMv5 hardware, so... */
#define load_write_pc_interworks true
#define test_load_write_pc_interworking()

#else /* __LINUX_ARM_ARCH__ < 6 */

/* We need run-time testing to determine if load_write_pc() should interwork. */
extern bool load_write_pc_interworks;
void __init test_load_write_pc_interworking(void);

#endif

static inline void __kprobes load_write_pc(long pcv, struct pt_regs *regs)
{
	if (load_write_pc_interworks)
		bx_write_pc(pcv, regs);
	else
		regs->ARM_pc = pcv;
}


#if __LINUX_ARM_ARCH__ >= 7

#define alu_write_pc_interworks true
#define test_alu_write_pc_interworking()

#elif __LINUX_ARM_ARCH__ <= 5

/* Kernels built for <= ARMv5 should never run on >= ARMv6 hardware, so... */
#define alu_write_pc_interworks false
#define test_alu_write_pc_interworking()

#else /* __LINUX_ARM_ARCH__ == 6 */

/* We could be an ARMv6 binary on ARMv7 hardware so we need a run-time check. */
extern bool alu_write_pc_interworks;
void __init test_alu_write_pc_interworking(void);

#endif /* __LINUX_ARM_ARCH__ == 6 */

static inline void __kprobes alu_write_pc(long pcv, struct pt_regs *regs)
{
	if (alu_write_pc_interworks)
		bx_write_pc(pcv, regs);
	else
		regs->ARM_pc = pcv;
}


/*
 * Test if load/store instructions writeback the address register.
 * if P (bit 24) == 0 or W (bit 21) == 1
 */
#define is_writeback(insn) ((insn ^ 0x01000000) & 0x01200000)

/*
 * The following definitions and macros are used to build instruction
 * decoding tables for use by probes_decode_insn.
 *
 * These tables are a concatenation of entries each of which consist of one of
 * the decode_* structs. All of the fields in every type of decode structure
 * are of the union type decode_item, therefore the entire decode table can be
 * viewed as an array of these and declared like:
 *
 *	static const union decode_item table_name[] = {};
 *
 * In order to construct each entry in the table, macros are used to
 * initialise a number of sequential decode_item values in a layout which
 * matches the relevant struct. E.g. DECODE_SIMULATE initialise a struct
 * decode_simulate by initialising four decode_item objects like this...
 *
 *	{.bits = _type},
 *	{.bits = _mask},
 *	{.bits = _value},
 *	{.action = _handler},
 *
 * Initialising a specified member of the union means that the compiler
 * will produce a warning if the argument is of an incorrect type.
 *
 * Below is a list of each of the macros used to initialise entries and a
 * description of the action performed when that entry is matched to an
 * instruction. A match is found when (instruction & mask) == value.
 *
 * DECODE_TABLE(mask, value, table)
 *	Instruction decoding jumps to parsing the new sub-table 'table'.
 *
 * DECODE_CUSTOM(mask, value, decoder)
 *	The value of 'decoder' is used as an index into the array of
 *	action functions, and the retrieved decoder function is invoked
 *	to complete decoding of the instruction.
 *
 * DECODE_SIMULATE(mask, value, handler)
 *	The probes instruction handler is set to the value found by
 *	indexing into the action array using the value of 'handler'. This
 *	will be used to simulate the instruction when the probe is hit.
 *	Decoding returns with INSN_GOOD_NO_SLOT.
 *
 * DECODE_EMULATE(mask, value, handler)
 *	The probes instruction handler is set to the value found by
 *	indexing into the action array using the value of 'handler'. This
 *	will be used to emulate the instruction when the probe is hit. The
 *	modified instruction (see below) is placed in the probes instruction
 *	slot so it may be called by the emulation code. Decoding returns
 *	with INSN_GOOD.
 *
 * DECODE_REJECT(mask, value)
 *	Instruction decoding fails with INSN_REJECTED
 *
 * DECODE_OR(mask, value)
 *	This allows the mask/value test of multiple table entries to be
 *	logically ORed. Once an 'or' entry is matched the decoding action to
 *	be performed is that of the next entry which isn't an 'or'. E.g.
 *
 *		DECODE_OR	(mask1, value1)
 *		DECODE_OR	(mask2, value2)
 *		DECODE_SIMULATE	(mask3, value3, simulation_handler)
 *
 *	This means that if any of the three mask/value pairs match the
 *	instruction being decoded, then 'simulation_handler' will be used
 *	for it.
 *
 * Both the SIMULATE and EMULATE macros have a second form which take an
 * additional 'regs' argument.
 *
 *	DECODE_SIMULATEX(mask, value, handler, regs)
 *	DECODE_EMULATEX	(mask, value, handler, regs)
 *
 * These are used to specify what kind of CPU register is encoded in each of the
 * least significant 5 nibbles of the instruction being decoded. The regs value
 * is specified using the REGS macro, this takes any of the REG_TYPE_* values
 * from enum decode_reg_type as arguments; only the '*' part of the name is
 * given. E.g.
 *
 *	REGS(0, ANY, NOPC, 0, ANY)
 *
 * This indicates an instruction is encoded like:
 *
 *	bits 19..16	ignore
 *	bits 15..12	any register allowed here
 *	bits 11.. 8	any register except PC allowed here
 *	bits  7.. 4	ignore
 *	bits  3.. 0	any register allowed here
 *
 * This register specification is checked after a decode table entry is found to
 * match an instruction (through the mask/value test). Any invalid register then
 * found in the instruction will cause decoding to fail with INSN_REJECTED. In
 * the above example this would happen if bits 11..8 of the instruction were
 * 1111, indicating R15 or PC.
 *
 * As well as checking for legal combinations of registers, this data is also
 * used to modify the registers encoded in the instructions so that an
 * emulation routines can use it. (See decode_regs() and INSN_NEW_BITS.)
 *
 * Here is a real example which matches ARM instructions of the form
 * "AND <Rd>,<Rn>,<Rm>,<shift> <Rs>"
 *
 *	DECODE_EMULATEX	(0x0e000090, 0x00000010, PROBES_DATA_PROCESSING_REG,
 *						 REGS(ANY, ANY, NOPC, 0, ANY)),
 *						      ^    ^    ^        ^
 *						      Rn   Rd   Rs       Rm
 *
 * Decoding the instruction "AND R4, R5, R6, ASL R15" will be rejected because
 * Rs == R15
 *
 * Decoding the instruction "AND R4, R5, R6, ASL R7" will be accepted and the
 * instruction will be modified to "AND R0, R2, R3, ASL R1" and then placed into
 * the kprobes instruction slot. This can then be called later by the handler
 * function emulate_rd12rn16rm0rs8_rwflags (a pointer to which is retrieved from
 * the indicated slot in the action array), in order to simulate the instruction.
 */

enum decode_type {
	DECODE_TYPE_END,
	DECODE_TYPE_TABLE,
	DECODE_TYPE_CUSTOM,
	DECODE_TYPE_SIMULATE,
	DECODE_TYPE_EMULATE,
	DECODE_TYPE_OR,
	DECODE_TYPE_REJECT,
	NUM_DECODE_TYPES /* Must be last enum */
};

#define DECODE_TYPE_BITS	4
#define DECODE_TYPE_MASK	((1 << DECODE_TYPE_BITS) - 1)

enum decode_reg_type {
	REG_TYPE_NONE = 0, /* Not a register, ignore */
	REG_TYPE_ANY,	   /* Any register allowed */
	REG_TYPE_SAMEAS16, /* Register should be same as that at bits 19..16 */
	REG_TYPE_SP,	   /* Register must be SP */
	REG_TYPE_PC,	   /* Register must be PC */
	REG_TYPE_NOSP,	   /* Register must not be SP */
	REG_TYPE_NOSPPC,   /* Register must not be SP or PC */
	REG_TYPE_NOPC,	   /* Register must not be PC */
	REG_TYPE_NOPCWB,   /* No PC if load/store write-back flag also set */

	/* The following types are used when the encoding for PC indicates
	 * another instruction form. This distiction only matters for test
	 * case coverage checks.
	 */
	REG_TYPE_NOPCX,	   /* Register must not be PC */
	REG_TYPE_NOSPPCX,  /* Register must not be SP or PC */

	/* Alias to allow '0' arg to be used in REGS macro. */
	REG_TYPE_0 = REG_TYPE_NONE
};

#define REGS(r16, r12, r8, r4, r0)	\
	(((REG_TYPE_##r16) << 16) +	\
	((REG_TYPE_##r12) << 12) +	\
	((REG_TYPE_##r8) << 8) +	\
	((REG_TYPE_##r4) << 4) +	\
	(REG_TYPE_##r0))

union decode_item {
	u32			bits;
	const union decode_item	*table;
	int			action;
};

struct decode_header;
typedef enum probes_insn (probes_custom_decode_t)(probes_opcode_t,
						  struct arch_probes_insn *,
						  const struct decode_header *);

union decode_action {
	probes_insn_handler_t	*handler;
	probes_custom_decode_t	*decoder;
};

typedef enum probes_insn (probes_check_t)(probes_opcode_t,
					   struct arch_probes_insn *,
					   const struct decode_header *);

struct decode_checker {
	probes_check_t	*checker;
};

#define DECODE_END			\
	{.bits = DECODE_TYPE_END}


struct decode_header {
	union decode_item	type_regs;
	union decode_item	mask;
	union decode_item	value;
};

#define DECODE_HEADER(_type, _mask, _value, _regs)		\
	{.bits = (_type) | ((_regs) << DECODE_TYPE_BITS)},	\
	{.bits = (_mask)},					\
	{.bits = (_value)}


struct decode_table {
	struct decode_header	header;
	union decode_item	table;
};

#define DECODE_TABLE(_mask, _value, _table)			\
	DECODE_HEADER(DECODE_TYPE_TABLE, _mask, _value, 0),	\
	{.table = (_table)}


struct decode_custom {
	struct decode_header	header;
	union decode_item	decoder;
};

#define DECODE_CUSTOM(_mask, _value, _decoder)			\
	DECODE_HEADER(DECODE_TYPE_CUSTOM, _mask, _value, 0),	\
	{.action = (_decoder)}


struct decode_simulate {
	struct decode_header	header;
	union decode_item	handler;
};

#define DECODE_SIMULATEX(_mask, _value, _handler, _regs)		\
	DECODE_HEADER(DECODE_TYPE_SIMULATE, _mask, _value, _regs),	\
	{.action = (_handler)}

#define DECODE_SIMULATE(_mask, _value, _handler)	\
	DECODE_SIMULATEX(_mask, _value, _handler, 0)


struct decode_emulate {
	struct decode_header	header;
	union decode_item	handler;
};

#define DECODE_EMULATEX(_mask, _value, _handler, _regs)			\
	DECODE_HEADER(DECODE_TYPE_EMULATE, _mask, _value, _regs),	\
	{.action = (_handler)}

#define DECODE_EMULATE(_mask, _value, _handler)		\
	DECODE_EMULATEX(_mask, _value, _handler, 0)


struct decode_or {
	struct decode_header	header;
};

#define DECODE_OR(_mask, _value)				\
	DECODE_HEADER(DECODE_TYPE_OR, _mask, _value, 0)

enum probes_insn {
	INSN_REJECTED,
	INSN_GOOD,
	INSN_GOOD_NO_SLOT
};

struct decode_reject {
	struct decode_header	header;
};

#define DECODE_REJECT(_mask, _value)				\
	DECODE_HEADER(DECODE_TYPE_REJECT, _mask, _value, 0)

probes_insn_handler_t probes_simulate_nop;
probes_insn_handler_t probes_emulate_none;

int __kprobes
probes_decode_insn(probes_opcode_t insn, struct arch_probes_insn *asi,
		const union decode_item *table, bool thumb, bool emulate,
		const union decode_action *actions,
		const struct decode_checker **checkers);

#endif
