/*
 * linux/arch/unicore32/mm/alignment.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
/*
 * TODO:
 *  FPU ldm/stm not handling
 */
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/uaccess.h>

#include <asm/tlbflush.h>
#include <asm/unaligned.h>

#include "mm.h"

#define CODING_BITS(i)	(i & 0xe0000120)

#define LDST_P_BIT(i)	(i & (1 << 28))	/* Preindex             */
#define LDST_U_BIT(i)	(i & (1 << 27))	/* Add offset           */
#define LDST_W_BIT(i)	(i & (1 << 25))	/* Writeback            */
#define LDST_L_BIT(i)	(i & (1 << 24))	/* Load                 */

#define LDST_P_EQ_U(i)	((((i) ^ ((i) >> 1)) & (1 << 27)) == 0)

#define LDSTH_I_BIT(i)	(i & (1 << 26))	/* half-word immed      */
#define LDM_S_BIT(i)	(i & (1 << 26))	/* write ASR from BSR */
#define LDM_H_BIT(i)	(i & (1 << 6))	/* select r0-r15 or r16-r31 */

#define RN_BITS(i)	((i >> 19) & 31)	/* Rn                   */
#define RD_BITS(i)	((i >> 14) & 31)	/* Rd                   */
#define RM_BITS(i)	(i & 31)	/* Rm                   */

#define REGMASK_BITS(i)	(((i & 0x7fe00) >> 3) | (i & 0x3f))
#define OFFSET_BITS(i)	(i & 0x03fff)

#define SHIFT_BITS(i)	((i >> 9) & 0x1f)
#define SHIFT_TYPE(i)	(i & 0xc0)
#define SHIFT_LSL	0x00
#define SHIFT_LSR	0x40
#define SHIFT_ASR	0x80
#define SHIFT_RORRRX	0xc0

union offset_union {
	unsigned long un;
	signed long sn;
};

#define TYPE_ERROR	0
#define TYPE_FAULT	1
#define TYPE_LDST	2
#define TYPE_DONE	3
#define TYPE_SWAP  4
#define TYPE_COLS  5		/* Coprocessor load/store */

#define get8_unaligned_check(val, addr, err)		\
	__asm__(					\
	"1:	ldb.u	%1, [%2], #1\n"			\
	"2:\n"						\
	"	.pushsection .fixup,\"ax\"\n"		\
	"	.align	2\n"				\
	"3:	mov	%0, #1\n"			\
	"	b	2b\n"				\
	"	.popsection\n"				\
	"	.pushsection __ex_table,\"a\"\n"		\
	"	.align	3\n"				\
	"	.long	1b, 3b\n"			\
	"	.popsection\n"				\
	: "=r" (err), "=&r" (val), "=r" (addr)		\
	: "0" (err), "2" (addr))

#define get8t_unaligned_check(val, addr, err)		\
	__asm__(					\
	"1:	ldb.u	%1, [%2], #1\n"			\
	"2:\n"						\
	"	.pushsection .fixup,\"ax\"\n"		\
	"	.align	2\n"				\
	"3:	mov	%0, #1\n"			\
	"	b	2b\n"				\
	"	.popsection\n"				\
	"	.pushsection __ex_table,\"a\"\n"		\
	"	.align	3\n"				\
	"	.long	1b, 3b\n"			\
	"	.popsection\n"				\
	: "=r" (err), "=&r" (val), "=r" (addr)		\
	: "0" (err), "2" (addr))

#define get16_unaligned_check(val, addr)			\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		get8_unaligned_check(val, a, err);		\
		get8_unaligned_check(v, a, err);		\
		val |= v << 8;					\
		if (err)					\
			goto fault;				\
	} while (0)

#define put16_unaligned_check(val, addr)			\
	do {							\
		unsigned int err = 0, v = val, a = addr;	\
		__asm__(					\
		"1:	stb.u	%1, [%2], #1\n"			\
		"	mov	%1, %1 >> #8\n"			\
		"2:	stb.u	%1, [%2]\n"			\
		"3:\n"						\
		"	.pushsection .fixup,\"ax\"\n"		\
		"	.align	2\n"				\
		"4:	mov	%0, #1\n"			\
		"	b	3b\n"				\
		"	.popsection\n"				\
		"	.pushsection __ex_table,\"a\"\n"		\
		"	.align	3\n"				\
		"	.long	1b, 4b\n"			\
		"	.long	2b, 4b\n"			\
		"	.popsection\n"				\
		: "=r" (err), "=&r" (v), "=&r" (a)		\
		: "0" (err), "1" (v), "2" (a));			\
		if (err)					\
			goto fault;				\
	} while (0)

#define __put32_unaligned_check(ins, val, addr)			\
	do {							\
		unsigned int err = 0, v = val, a = addr;	\
		__asm__(					\
		"1:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1 >> #8\n"			\
		"2:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1 >> #8\n"			\
		"3:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1 >> #8\n"			\
		"4:	"ins"	%1, [%2]\n"			\
		"5:\n"						\
		"	.pushsection .fixup,\"ax\"\n"		\
		"	.align	2\n"				\
		"6:	mov	%0, #1\n"			\
		"	b	5b\n"				\
		"	.popsection\n"				\
		"	.pushsection __ex_table,\"a\"\n"		\
		"	.align	3\n"				\
		"	.long	1b, 6b\n"			\
		"	.long	2b, 6b\n"			\
		"	.long	3b, 6b\n"			\
		"	.long	4b, 6b\n"			\
		"	.popsection\n"				\
		: "=r" (err), "=&r" (v), "=&r" (a)		\
		: "0" (err), "1" (v), "2" (a));			\
		if (err)					\
			goto fault;				\
	} while (0)

#define get32_unaligned_check(val, addr)			\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		get8_unaligned_check(val, a, err);		\
		get8_unaligned_check(v, a, err);		\
		val |= v << 8;					\
		get8_unaligned_check(v, a, err);		\
		val |= v << 16;					\
		get8_unaligned_check(v, a, err);		\
		val |= v << 24;					\
		if (err)					\
			goto fault;				\
	} while (0)

#define put32_unaligned_check(val, addr)			\
	__put32_unaligned_check("stb.u", val, addr)

#define get32t_unaligned_check(val, addr)			\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		get8t_unaligned_check(val, a, err);		\
		get8t_unaligned_check(v, a, err);		\
		val |= v << 8;					\
		get8t_unaligned_check(v, a, err);		\
		val |= v << 16;					\
		get8t_unaligned_check(v, a, err);		\
		val |= v << 24;					\
		if (err)					\
			goto fault;				\
	} while (0)

#define put32t_unaligned_check(val, addr)			\
	__put32_unaligned_check("stb.u", val, addr)

static void
do_alignment_finish_ldst(unsigned long addr, unsigned long instr,
			 struct pt_regs *regs, union offset_union offset)
{
	if (!LDST_U_BIT(instr))
		offset.un = -offset.un;

	if (!LDST_P_BIT(instr))
		addr += offset.un;

	if (!LDST_P_BIT(instr) || LDST_W_BIT(instr))
		regs->uregs[RN_BITS(instr)] = addr;
}

static int
do_alignment_ldrhstrh(unsigned long addr, unsigned long instr,
		      struct pt_regs *regs)
{
	unsigned int rd = RD_BITS(instr);

	/* old value 0x40002120, can't judge swap instr correctly */
	if ((instr & 0x4b003fe0) == 0x40000120)
		goto swp;

	if (LDST_L_BIT(instr)) {
		unsigned long val;
		get16_unaligned_check(val, addr);

		/* signed half-word? */
		if (instr & 0x80)
			val = (signed long)((signed short)val);

		regs->uregs[rd] = val;
	} else
		put16_unaligned_check(regs->uregs[rd], addr);

	return TYPE_LDST;

swp:
	/* only handle swap word
	 * for swap byte should not active this alignment exception */
	get32_unaligned_check(regs->uregs[RD_BITS(instr)], addr);
	put32_unaligned_check(regs->uregs[RM_BITS(instr)], addr);
	return TYPE_SWAP;

fault:
	return TYPE_FAULT;
}

static int
do_alignment_ldrstr(unsigned long addr, unsigned long instr,
		    struct pt_regs *regs)
{
	unsigned int rd = RD_BITS(instr);

	if (!LDST_P_BIT(instr) && LDST_W_BIT(instr))
		goto trans;

	if (LDST_L_BIT(instr))
		get32_unaligned_check(regs->uregs[rd], addr);
	else
		put32_unaligned_check(regs->uregs[rd], addr);
	return TYPE_LDST;

trans:
	if (LDST_L_BIT(instr))
		get32t_unaligned_check(regs->uregs[rd], addr);
	else
		put32t_unaligned_check(regs->uregs[rd], addr);
	return TYPE_LDST;

fault:
	return TYPE_FAULT;
}

/*
 * LDM/STM alignment handler.
 *
 * There are 4 variants of this instruction:
 *
 * B = rn pointer before instruction, A = rn pointer after instruction
 *              ------ increasing address ----->
 *	        |    | r0 | r1 | ... | rx |    |
 * PU = 01             B                    A
 * PU = 11        B                    A
 * PU = 00        A                    B
 * PU = 10             A                    B
 */
static int
do_alignment_ldmstm(unsigned long addr, unsigned long instr,
		    struct pt_regs *regs)
{
	unsigned int rd, rn, pc_correction, reg_correction, nr_regs, regbits;
	unsigned long eaddr, newaddr;

	if (LDM_S_BIT(instr))
		goto bad;

	pc_correction = 4;	/* processor implementation defined */

	/* count the number of registers in the mask to be transferred */
	nr_regs = hweight16(REGMASK_BITS(instr)) * 4;

	rn = RN_BITS(instr);
	newaddr = eaddr = regs->uregs[rn];

	if (!LDST_U_BIT(instr))
		nr_regs = -nr_regs;
	newaddr += nr_regs;
	if (!LDST_U_BIT(instr))
		eaddr = newaddr;

	if (LDST_P_EQ_U(instr))	/* U = P */
		eaddr += 4;

	/*
	 * This is a "hint" - we already have eaddr worked out by the
	 * processor for us.
	 */
	if (addr != eaddr) {
		printk(KERN_ERR "LDMSTM: PC = %08lx, instr = %08lx, "
		       "addr = %08lx, eaddr = %08lx\n",
		       instruction_pointer(regs), instr, addr, eaddr);
		show_regs(regs);
	}

	if (LDM_H_BIT(instr))
		reg_correction = 0x10;
	else
		reg_correction = 0x00;

	for (regbits = REGMASK_BITS(instr), rd = 0; regbits;
	     regbits >>= 1, rd += 1)
		if (regbits & 1) {
			if (LDST_L_BIT(instr))
				get32_unaligned_check(regs->
					uregs[rd + reg_correction], eaddr);
			else
				put32_unaligned_check(regs->
					uregs[rd + reg_correction], eaddr);
			eaddr += 4;
		}

	if (LDST_W_BIT(instr))
		regs->uregs[rn] = newaddr;
	return TYPE_DONE;

fault:
	regs->UCreg_pc -= pc_correction;
	return TYPE_FAULT;

bad:
	printk(KERN_ERR "Alignment trap: not handling ldm with s-bit set\n");
	return TYPE_ERROR;
}

static int
do_alignment(unsigned long addr, unsigned int error_code, struct pt_regs *regs)
{
	union offset_union offset;
	unsigned long instr, instrptr;
	int (*handler) (unsigned long addr, unsigned long instr,
			struct pt_regs *regs);
	unsigned int type;

	instrptr = instruction_pointer(regs);
	if (instrptr >= PAGE_OFFSET)
		instr = *(unsigned long *)instrptr;
	else {
		__asm__ __volatile__(
				"ldw.u	%0, [%1]\n"
				: "=&r"(instr)
				: "r"(instrptr));
	}

	regs->UCreg_pc += 4;

	switch (CODING_BITS(instr)) {
	case 0x40000120:	/* ldrh or strh */
		if (LDSTH_I_BIT(instr))
			offset.un = (instr & 0x3e00) >> 4 | (instr & 31);
		else
			offset.un = regs->uregs[RM_BITS(instr)];
		handler = do_alignment_ldrhstrh;
		break;

	case 0x60000000:	/* ldr or str immediate */
	case 0x60000100:	/* ldr or str immediate */
	case 0x60000020:	/* ldr or str immediate */
	case 0x60000120:	/* ldr or str immediate */
		offset.un = OFFSET_BITS(instr);
		handler = do_alignment_ldrstr;
		break;

	case 0x40000000:	/* ldr or str register */
		offset.un = regs->uregs[RM_BITS(instr)];
		{
			unsigned int shiftval = SHIFT_BITS(instr);

			switch (SHIFT_TYPE(instr)) {
			case SHIFT_LSL:
				offset.un <<= shiftval;
				break;

			case SHIFT_LSR:
				offset.un >>= shiftval;
				break;

			case SHIFT_ASR:
				offset.sn >>= shiftval;
				break;

			case SHIFT_RORRRX:
				if (shiftval == 0) {
					offset.un >>= 1;
					if (regs->UCreg_asr & PSR_C_BIT)
						offset.un |= 1 << 31;
				} else
					offset.un = offset.un >> shiftval |
					    offset.un << (32 - shiftval);
				break;
			}
		}
		handler = do_alignment_ldrstr;
		break;

	case 0x80000000:	/* ldm or stm */
	case 0x80000020:	/* ldm or stm */
		handler = do_alignment_ldmstm;
		break;

	default:
		goto bad;
	}

	type = handler(addr, instr, regs);

	if (type == TYPE_ERROR || type == TYPE_FAULT)
		goto bad_or_fault;

	if (type == TYPE_LDST)
		do_alignment_finish_ldst(addr, instr, regs, offset);

	return 0;

bad_or_fault:
	if (type == TYPE_ERROR)
		goto bad;
	regs->UCreg_pc -= 4;
	/*
	 * We got a fault - fix it up, or die.
	 */
	do_bad_area(addr, error_code, regs);
	return 0;

bad:
	/*
	 * Oops, we didn't handle the instruction.
	 * However, we must handle fpu instr firstly.
	 */
#ifdef CONFIG_UNICORE_FPU_F64
	/* handle co.load/store */
#define CODING_COLS                0xc0000000
#define COLS_OFFSET_BITS(i)	(i & 0x1FF)
#define COLS_L_BITS(i)		(i & (1<<24))
#define COLS_FN_BITS(i)		((i>>14) & 31)
	if ((instr & 0xe0000000) == CODING_COLS) {
		unsigned int fn = COLS_FN_BITS(instr);
		unsigned long val = 0;
		if (COLS_L_BITS(instr)) {
			get32t_unaligned_check(val, addr);
			switch (fn) {
#define ASM_MTF(n)	case n:						\
			__asm__ __volatile__("MTF %0, F" __stringify(n)	\
				: : "r"(val));				\
			break;
			ASM_MTF(0); ASM_MTF(1); ASM_MTF(2); ASM_MTF(3);
			ASM_MTF(4); ASM_MTF(5); ASM_MTF(6); ASM_MTF(7);
			ASM_MTF(8); ASM_MTF(9); ASM_MTF(10); ASM_MTF(11);
			ASM_MTF(12); ASM_MTF(13); ASM_MTF(14); ASM_MTF(15);
			ASM_MTF(16); ASM_MTF(17); ASM_MTF(18); ASM_MTF(19);
			ASM_MTF(20); ASM_MTF(21); ASM_MTF(22); ASM_MTF(23);
			ASM_MTF(24); ASM_MTF(25); ASM_MTF(26); ASM_MTF(27);
			ASM_MTF(28); ASM_MTF(29); ASM_MTF(30); ASM_MTF(31);
#undef ASM_MTF
			}
		} else {
			switch (fn) {
#define ASM_MFF(n)	case n:						\
			__asm__ __volatile__("MFF %0, F" __stringify(n)	\
				: : "r"(val));				\
			break;
			ASM_MFF(0); ASM_MFF(1); ASM_MFF(2); ASM_MFF(3);
			ASM_MFF(4); ASM_MFF(5); ASM_MFF(6); ASM_MFF(7);
			ASM_MFF(8); ASM_MFF(9); ASM_MFF(10); ASM_MFF(11);
			ASM_MFF(12); ASM_MFF(13); ASM_MFF(14); ASM_MFF(15);
			ASM_MFF(16); ASM_MFF(17); ASM_MFF(18); ASM_MFF(19);
			ASM_MFF(20); ASM_MFF(21); ASM_MFF(22); ASM_MFF(23);
			ASM_MFF(24); ASM_MFF(25); ASM_MFF(26); ASM_MFF(27);
			ASM_MFF(28); ASM_MFF(29); ASM_MFF(30); ASM_MFF(31);
#undef ASM_MFF
			}
			put32t_unaligned_check(val, addr);
		}
		return TYPE_COLS;
	}
fault:
	return TYPE_FAULT;
#endif
	printk(KERN_ERR "Alignment trap: not handling instruction "
	       "%08lx at [<%08lx>]\n", instr, instrptr);
	return 1;
}

/*
 * This needs to be done after sysctl_init, otherwise sys/ will be
 * overwritten.  Actually, this shouldn't be in sys/ at all since
 * it isn't a sysctl, and it doesn't contain sysctl information.
 */
static int __init alignment_init(void)
{
	hook_fault_code(1, do_alignment, SIGBUS, BUS_ADRALN,
			"alignment exception");

	return 0;
}

fs_initcall(alignment_init);
