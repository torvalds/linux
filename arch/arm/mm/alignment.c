/*
 *  linux/arch/arm/mm/alignment.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-2001 Russell King
 *  Thumb aligment fault fixups (c) 2004 MontaVista Software, Inc.
 *  - Adapted from gdb/sim/arm/thumbemu.c -- Thumb instruction emulation.
 *    Copyright (C) 1996, Cygnus Software Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>

#include "fault.h"

/*
 * 32-bit misaligned trap handler (c) 1998 San Mehat (CCC) -July 1998
 * /proc/sys/debug/alignment, modified and integrated into
 * Linux 2.1 by Russell King
 *
 * Speed optimisations and better fault handling by Russell King.
 *
 * *** NOTE ***
 * This code is not portable to processors with late data abort handling.
 */
#define CODING_BITS(i)	(i & 0x0e000000)

#define LDST_I_BIT(i)	(i & (1 << 26))		/* Immediate constant	*/
#define LDST_P_BIT(i)	(i & (1 << 24))		/* Preindex		*/
#define LDST_U_BIT(i)	(i & (1 << 23))		/* Add offset		*/
#define LDST_W_BIT(i)	(i & (1 << 21))		/* Writeback		*/
#define LDST_L_BIT(i)	(i & (1 << 20))		/* Load			*/

#define LDST_P_EQ_U(i)	((((i) ^ ((i) >> 1)) & (1 << 23)) == 0)

#define LDSTHD_I_BIT(i)	(i & (1 << 22))		/* double/half-word immed */
#define LDM_S_BIT(i)	(i & (1 << 22))		/* write CPSR from SPSR	*/

#define RN_BITS(i)	((i >> 16) & 15)	/* Rn			*/
#define RD_BITS(i)	((i >> 12) & 15)	/* Rd			*/
#define RM_BITS(i)	(i & 15)		/* Rm			*/

#define REGMASK_BITS(i)	(i & 0xffff)
#define OFFSET_BITS(i)	(i & 0x0fff)

#define IS_SHIFT(i)	(i & 0x0ff0)
#define SHIFT_BITS(i)	((i >> 7) & 0x1f)
#define SHIFT_TYPE(i)	(i & 0x60)
#define SHIFT_LSL	0x00
#define SHIFT_LSR	0x20
#define SHIFT_ASR	0x40
#define SHIFT_RORRRX	0x60

static unsigned long ai_user;
static unsigned long ai_sys;
static unsigned long ai_skipped;
static unsigned long ai_half;
static unsigned long ai_word;
static unsigned long ai_dword;
static unsigned long ai_multi;
static int ai_usermode;

#ifdef CONFIG_PROC_FS
static const char *usermode_action[] = {
	"ignored",
	"warn",
	"fixup",
	"fixup+warn",
	"signal",
	"signal+warn"
};

static int
proc_alignment_read(char *page, char **start, off_t off, int count, int *eof,
		    void *data)
{
	char *p = page;
	int len;

	p += sprintf(p, "User:\t\t%lu\n", ai_user);
	p += sprintf(p, "System:\t\t%lu\n", ai_sys);
	p += sprintf(p, "Skipped:\t%lu\n", ai_skipped);
	p += sprintf(p, "Half:\t\t%lu\n", ai_half);
	p += sprintf(p, "Word:\t\t%lu\n", ai_word);
	if (cpu_architecture() >= CPU_ARCH_ARMv5TE)
		p += sprintf(p, "DWord:\t\t%lu\n", ai_dword);
	p += sprintf(p, "Multi:\t\t%lu\n", ai_multi);
	p += sprintf(p, "User faults:\t%i (%s)\n", ai_usermode,
			usermode_action[ai_usermode]);

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

static int proc_alignment_write(struct file *file, const char __user *buffer,
				unsigned long count, void *data)
{
	char mode;

	if (count > 0) {
		if (get_user(mode, buffer))
			return -EFAULT;
		if (mode >= '0' && mode <= '5')
			ai_usermode = mode - '0';
	}
	return count;
}

#endif /* CONFIG_PROC_FS */

union offset_union {
	unsigned long un;
	  signed long sn;
};

#define TYPE_ERROR	0
#define TYPE_FAULT	1
#define TYPE_LDST	2
#define TYPE_DONE	3

#ifdef __ARMEB__
#define BE		1
#define FIRST_BYTE_16	"mov	%1, %1, ror #8\n"
#define FIRST_BYTE_32	"mov	%1, %1, ror #24\n"
#define NEXT_BYTE	"ror #24"
#else
#define BE		0
#define FIRST_BYTE_16
#define FIRST_BYTE_32
#define NEXT_BYTE	"lsr #8"
#endif

#define __get8_unaligned_check(ins,val,addr,err)	\
	__asm__(					\
	"1:	"ins"	%1, [%2], #1\n"			\
	"2:\n"						\
	"	.section .fixup,\"ax\"\n"		\
	"	.align	2\n"				\
	"3:	mov	%0, #1\n"			\
	"	b	2b\n"				\
	"	.previous\n"				\
	"	.section __ex_table,\"a\"\n"		\
	"	.align	3\n"				\
	"	.long	1b, 3b\n"			\
	"	.previous\n"				\
	: "=r" (err), "=&r" (val), "=r" (addr)		\
	: "0" (err), "2" (addr))

#define __get16_unaligned_check(ins,val,addr)			\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		__get8_unaligned_check(ins,v,a,err);		\
		val =  v << ((BE) ? 8 : 0);			\
		__get8_unaligned_check(ins,v,a,err);		\
		val |= v << ((BE) ? 0 : 8);			\
		if (err)					\
			goto fault;				\
	} while (0)

#define get16_unaligned_check(val,addr) \
	__get16_unaligned_check("ldrb",val,addr)

#define get16t_unaligned_check(val,addr) \
	__get16_unaligned_check("ldrbt",val,addr)

#define __get32_unaligned_check(ins,val,addr)			\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		__get8_unaligned_check(ins,v,a,err);		\
		val =  v << ((BE) ? 24 :  0);			\
		__get8_unaligned_check(ins,v,a,err);		\
		val |= v << ((BE) ? 16 :  8);			\
		__get8_unaligned_check(ins,v,a,err);		\
		val |= v << ((BE) ?  8 : 16);			\
		__get8_unaligned_check(ins,v,a,err);		\
		val |= v << ((BE) ?  0 : 24);			\
		if (err)					\
			goto fault;				\
	} while (0)

#define get32_unaligned_check(val,addr) \
	__get32_unaligned_check("ldrb",val,addr)

#define get32t_unaligned_check(val,addr) \
	__get32_unaligned_check("ldrbt",val,addr)

#define __put16_unaligned_check(ins,val,addr)			\
	do {							\
		unsigned int err = 0, v = val, a = addr;	\
		__asm__( FIRST_BYTE_16				\
		"1:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1, "NEXT_BYTE"\n"		\
		"2:	"ins"	%1, [%2]\n"			\
		"3:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.align	2\n"				\
		"4:	mov	%0, #1\n"			\
		"	b	3b\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.align	3\n"				\
		"	.long	1b, 4b\n"			\
		"	.long	2b, 4b\n"			\
		"	.previous\n"				\
		: "=r" (err), "=&r" (v), "=&r" (a)		\
		: "0" (err), "1" (v), "2" (a));			\
		if (err)					\
			goto fault;				\
	} while (0)

#define put16_unaligned_check(val,addr)  \
	__put16_unaligned_check("strb",val,addr)

#define put16t_unaligned_check(val,addr) \
	__put16_unaligned_check("strbt",val,addr)

#define __put32_unaligned_check(ins,val,addr)			\
	do {							\
		unsigned int err = 0, v = val, a = addr;	\
		__asm__( FIRST_BYTE_32				\
		"1:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1, "NEXT_BYTE"\n"		\
		"2:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1, "NEXT_BYTE"\n"		\
		"3:	"ins"	%1, [%2], #1\n"			\
		"	mov	%1, %1, "NEXT_BYTE"\n"		\
		"4:	"ins"	%1, [%2]\n"			\
		"5:\n"						\
		"	.section .fixup,\"ax\"\n"		\
		"	.align	2\n"				\
		"6:	mov	%0, #1\n"			\
		"	b	5b\n"				\
		"	.previous\n"				\
		"	.section __ex_table,\"a\"\n"		\
		"	.align	3\n"				\
		"	.long	1b, 6b\n"			\
		"	.long	2b, 6b\n"			\
		"	.long	3b, 6b\n"			\
		"	.long	4b, 6b\n"			\
		"	.previous\n"				\
		: "=r" (err), "=&r" (v), "=&r" (a)		\
		: "0" (err), "1" (v), "2" (a));			\
		if (err)					\
			goto fault;				\
	} while (0)

#define put32_unaligned_check(val,addr) \
	__put32_unaligned_check("strb", val, addr)

#define put32t_unaligned_check(val,addr) \
	__put32_unaligned_check("strbt", val, addr)

static void
do_alignment_finish_ldst(unsigned long addr, unsigned long instr, struct pt_regs *regs, union offset_union offset)
{
	if (!LDST_U_BIT(instr))
		offset.un = -offset.un;

	if (!LDST_P_BIT(instr))
		addr += offset.un;

	if (!LDST_P_BIT(instr) || LDST_W_BIT(instr))
		regs->uregs[RN_BITS(instr)] = addr;
}

static int
do_alignment_ldrhstrh(unsigned long addr, unsigned long instr, struct pt_regs *regs)
{
	unsigned int rd = RD_BITS(instr);

	ai_half += 1;

	if (user_mode(regs))
		goto user;

	if (LDST_L_BIT(instr)) {
		unsigned long val;
		get16_unaligned_check(val, addr);

		/* signed half-word? */
		if (instr & 0x40)
			val = (signed long)((signed short) val);

		regs->uregs[rd] = val;
	} else
		put16_unaligned_check(regs->uregs[rd], addr);

	return TYPE_LDST;

 user:
	if (LDST_L_BIT(instr)) {
		unsigned long val;
		get16t_unaligned_check(val, addr);

		/* signed half-word? */
		if (instr & 0x40)
			val = (signed long)((signed short) val);

		regs->uregs[rd] = val;
	} else
		put16t_unaligned_check(regs->uregs[rd], addr);

	return TYPE_LDST;

 fault:
	return TYPE_FAULT;
}

static int
do_alignment_ldrdstrd(unsigned long addr, unsigned long instr,
		      struct pt_regs *regs)
{
	unsigned int rd = RD_BITS(instr);

	if (((rd & 1) == 1) || (rd == 14))
		goto bad;

	ai_dword += 1;

	if (user_mode(regs))
		goto user;

	if ((instr & 0xf0) == 0xd0) {
		unsigned long val;
		get32_unaligned_check(val, addr);
		regs->uregs[rd] = val;
		get32_unaligned_check(val, addr + 4);
		regs->uregs[rd + 1] = val;
	} else {
		put32_unaligned_check(regs->uregs[rd], addr);
		put32_unaligned_check(regs->uregs[rd + 1], addr + 4);
	}

	return TYPE_LDST;

 user:
	if ((instr & 0xf0) == 0xd0) {
		unsigned long val;
		get32t_unaligned_check(val, addr);
		regs->uregs[rd] = val;
		get32t_unaligned_check(val, addr + 4);
		regs->uregs[rd + 1] = val;
	} else {
		put32t_unaligned_check(regs->uregs[rd], addr);
		put32t_unaligned_check(regs->uregs[rd + 1], addr + 4);
	}

	return TYPE_LDST;
 bad:
	return TYPE_ERROR;
 fault:
	return TYPE_FAULT;
}

static int
do_alignment_ldrstr(unsigned long addr, unsigned long instr, struct pt_regs *regs)
{
	unsigned int rd = RD_BITS(instr);

	ai_word += 1;

	if ((!LDST_P_BIT(instr) && LDST_W_BIT(instr)) || user_mode(regs))
		goto trans;

	if (LDST_L_BIT(instr)) {
		unsigned int val;
		get32_unaligned_check(val, addr);
		regs->uregs[rd] = val;
	} else
		put32_unaligned_check(regs->uregs[rd], addr);
	return TYPE_LDST;

 trans:
	if (LDST_L_BIT(instr)) {
		unsigned int val;
		get32t_unaligned_check(val, addr);
		regs->uregs[rd] = val;
	} else
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
do_alignment_ldmstm(unsigned long addr, unsigned long instr, struct pt_regs *regs)
{
	unsigned int rd, rn, correction, nr_regs, regbits;
	unsigned long eaddr, newaddr;

	if (LDM_S_BIT(instr))
		goto bad;

	correction = 4; /* processor implementation defined */
	regs->ARM_pc += correction;

	ai_multi += 1;

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
	 * For alignment faults on the ARM922T/ARM920T the MMU  makes
	 * the FSR (and hence addr) equal to the updated base address
	 * of the multiple access rather than the restored value.
	 * Switch this message off if we've got a ARM92[02], otherwise
	 * [ls]dm alignment faults are noisy!
	 */
#if !(defined CONFIG_CPU_ARM922T)  && !(defined CONFIG_CPU_ARM920T)
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
#endif

	if (user_mode(regs)) {
		for (regbits = REGMASK_BITS(instr), rd = 0; regbits;
		     regbits >>= 1, rd += 1)
			if (regbits & 1) {
				if (LDST_L_BIT(instr)) {
					unsigned int val;
					get32t_unaligned_check(val, eaddr);
					regs->uregs[rd] = val;
				} else
					put32t_unaligned_check(regs->uregs[rd], eaddr);
				eaddr += 4;
			}
	} else {
		for (regbits = REGMASK_BITS(instr), rd = 0; regbits;
		     regbits >>= 1, rd += 1)
			if (regbits & 1) {
				if (LDST_L_BIT(instr)) {
					unsigned int val;
					get32_unaligned_check(val, eaddr);
					regs->uregs[rd] = val;
				} else
					put32_unaligned_check(regs->uregs[rd], eaddr);
				eaddr += 4;
			}
	}

	if (LDST_W_BIT(instr))
		regs->uregs[rn] = newaddr;
	if (!LDST_L_BIT(instr) || !(REGMASK_BITS(instr) & (1 << 15)))
		regs->ARM_pc -= correction;
	return TYPE_DONE;

fault:
	regs->ARM_pc -= correction;
	return TYPE_FAULT;

bad:
	printk(KERN_ERR "Alignment trap: not handling ldm with s-bit set\n");
	return TYPE_ERROR;
}

/*
 * Convert Thumb ld/st instruction forms to equivalent ARM instructions so
 * we can reuse ARM userland alignment fault fixups for Thumb.
 *
 * This implementation was initially based on the algorithm found in
 * gdb/sim/arm/thumbemu.c. It is basically just a code reduction of same
 * to convert only Thumb ld/st instruction forms to equivalent ARM forms.
 *
 * NOTES:
 * 1. Comments below refer to ARM ARM DDI0100E Thumb Instruction sections.
 * 2. If for some reason we're passed an non-ld/st Thumb instruction to
 *    decode, we return 0xdeadc0de. This should never happen under normal
 *    circumstances but if it does, we've got other problems to deal with
 *    elsewhere and we obviously can't fix those problems here.
 */

static unsigned long
thumb2arm(u16 tinstr)
{
	u32 L = (tinstr & (1<<11)) >> 11;

	switch ((tinstr & 0xf800) >> 11) {
	/* 6.5.1 Format 1: */
	case 0x6000 >> 11:				/* 7.1.52 STR(1) */
	case 0x6800 >> 11:				/* 7.1.26 LDR(1) */
	case 0x7000 >> 11:				/* 7.1.55 STRB(1) */
	case 0x7800 >> 11:				/* 7.1.30 LDRB(1) */
		return 0xe5800000 |
			((tinstr & (1<<12)) << (22-12)) |	/* fixup */
			(L<<20) |				/* L==1? */
			((tinstr & (7<<0)) << (12-0)) |		/* Rd */
			((tinstr & (7<<3)) << (16-3)) |		/* Rn */
			((tinstr & (31<<6)) >>			/* immed_5 */
				(6 - ((tinstr & (1<<12)) ? 0 : 2)));
	case 0x8000 >> 11:				/* 7.1.57 STRH(1) */
	case 0x8800 >> 11:				/* 7.1.32 LDRH(1) */
		return 0xe1c000b0 |
			(L<<20) |				/* L==1? */
			((tinstr & (7<<0)) << (12-0)) |		/* Rd */
			((tinstr & (7<<3)) << (16-3)) |		/* Rn */
			((tinstr & (7<<6)) >> (6-1)) |	 /* immed_5[2:0] */
			((tinstr & (3<<9)) >> (9-8));	 /* immed_5[4:3] */

	/* 6.5.1 Format 2: */
	case 0x5000 >> 11:
	case 0x5800 >> 11:
		{
			static const u32 subset[8] = {
				0xe7800000,		/* 7.1.53 STR(2) */
				0xe18000b0,		/* 7.1.58 STRH(2) */
				0xe7c00000,		/* 7.1.56 STRB(2) */
				0xe19000d0,		/* 7.1.34 LDRSB */
				0xe7900000,		/* 7.1.27 LDR(2) */
				0xe19000b0,		/* 7.1.33 LDRH(2) */
				0xe7d00000,		/* 7.1.31 LDRB(2) */
				0xe19000f0		/* 7.1.35 LDRSH */
			};
			return subset[(tinstr & (7<<9)) >> 9] |
			    ((tinstr & (7<<0)) << (12-0)) |	/* Rd */
			    ((tinstr & (7<<3)) << (16-3)) |	/* Rn */
			    ((tinstr & (7<<6)) >> (6-0));	/* Rm */
		}

	/* 6.5.1 Format 3: */
	case 0x4800 >> 11:				/* 7.1.28 LDR(3) */
		/* NOTE: This case is not technically possible. We're
		 *	 loading 32-bit memory data via PC relative
		 *	 addressing mode. So we can and should eliminate
		 *	 this case. But I'll leave it here for now.
		 */
		return 0xe59f0000 |
		    ((tinstr & (7<<8)) << (12-8)) |		/* Rd */
		    ((tinstr & 255) << (2-0));			/* immed_8 */

	/* 6.5.1 Format 4: */
	case 0x9000 >> 11:				/* 7.1.54 STR(3) */
	case 0x9800 >> 11:				/* 7.1.29 LDR(4) */
		return 0xe58d0000 |
			(L<<20) |				/* L==1? */
			((tinstr & (7<<8)) << (12-8)) |		/* Rd */
			((tinstr & 255) << 2);			/* immed_8 */

	/* 6.6.1 Format 1: */
	case 0xc000 >> 11:				/* 7.1.51 STMIA */
	case 0xc800 >> 11:				/* 7.1.25 LDMIA */
		{
			u32 Rn = (tinstr & (7<<8)) >> 8;
			u32 W = ((L<<Rn) & (tinstr&255)) ? 0 : 1<<21;

			return 0xe8800000 | W | (L<<20) | (Rn<<16) |
				(tinstr&255);
		}

	/* 6.6.1 Format 2: */
	case 0xb000 >> 11:				/* 7.1.48 PUSH */
	case 0xb800 >> 11:				/* 7.1.47 POP */
		if ((tinstr & (3 << 9)) == 0x0400) {
			static const u32 subset[4] = {
				0xe92d0000,	/* STMDB sp!,{registers} */
				0xe92d4000,	/* STMDB sp!,{registers,lr} */
				0xe8bd0000,	/* LDMIA sp!,{registers} */
				0xe8bd8000	/* LDMIA sp!,{registers,pc} */
			};
			return subset[(L<<1) | ((tinstr & (1<<8)) >> 8)] |
			    (tinstr & 255);		/* register_list */
		}
		/* Else fall through for illegal instruction case */

	default:
		return 0xdeadc0de;
	}
}

static int
do_alignment(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	union offset_union offset;
	unsigned long instr = 0, instrptr;
	int (*handler)(unsigned long addr, unsigned long instr, struct pt_regs *regs);
	unsigned int type;
	mm_segment_t fs;
	unsigned int fault;
	u16 tinstr = 0;

	instrptr = instruction_pointer(regs);

	fs = get_fs();
	set_fs(KERNEL_DS);
	if thumb_mode(regs) {
		fault = __get_user(tinstr, (u16 *)(instrptr & ~1));
		if (!(fault))
			instr = thumb2arm(tinstr);
	} else
		fault = __get_user(instr, (u32 *)instrptr);
	set_fs(fs);

	if (fault) {
		type = TYPE_FAULT;
		goto bad_or_fault;
	}

	if (user_mode(regs))
		goto user;

	ai_sys += 1;

 fixup:

	regs->ARM_pc += thumb_mode(regs) ? 2 : 4;

	switch (CODING_BITS(instr)) {
	case 0x00000000:	/* 3.13.4 load/store instruction extensions */
		if (LDSTHD_I_BIT(instr))
			offset.un = (instr & 0xf00) >> 4 | (instr & 15);
		else
			offset.un = regs->uregs[RM_BITS(instr)];

		if ((instr & 0x000000f0) == 0x000000b0 || /* LDRH, STRH */
		    (instr & 0x001000f0) == 0x001000f0)   /* LDRSH */
			handler = do_alignment_ldrhstrh;
		else if ((instr & 0x001000f0) == 0x000000d0 || /* LDRD */
			 (instr & 0x001000f0) == 0x000000f0)   /* STRD */
			handler = do_alignment_ldrdstrd;
		else if ((instr & 0x01f00ff0) == 0x01000090) /* SWP */
			goto swp;
		else
			goto bad;
		break;

	case 0x04000000:	/* ldr or str immediate */
		offset.un = OFFSET_BITS(instr);
		handler = do_alignment_ldrstr;
		break;

	case 0x06000000:	/* ldr or str register */
		offset.un = regs->uregs[RM_BITS(instr)];

		if (IS_SHIFT(instr)) {
			unsigned int shiftval = SHIFT_BITS(instr);

			switch(SHIFT_TYPE(instr)) {
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
					if (regs->ARM_cpsr & PSR_C_BIT)
						offset.un |= 1 << 31;
				} else
					offset.un = offset.un >> shiftval |
							  offset.un << (32 - shiftval);
				break;
			}
		}
		handler = do_alignment_ldrstr;
		break;

	case 0x08000000:	/* ldm or stm */
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
	regs->ARM_pc -= thumb_mode(regs) ? 2 : 4;
	/*
	 * We got a fault - fix it up, or die.
	 */
	do_bad_area(addr, fsr, regs);
	return 0;

 swp:
	printk(KERN_ERR "Alignment trap: not handling swp instruction\n");

 bad:
	/*
	 * Oops, we didn't handle the instruction.
	 */
	printk(KERN_ERR "Alignment trap: not handling instruction "
		"%0*lx at [<%08lx>]\n",
		thumb_mode(regs) ? 4 : 8,
		thumb_mode(regs) ? tinstr : instr, instrptr);
	ai_skipped += 1;
	return 1;

 user:
	ai_user += 1;

	if (ai_usermode & 1)
		printk("Alignment trap: %s (%d) PC=0x%08lx Instr=0x%0*lx "
		       "Address=0x%08lx FSR 0x%03x\n", current->comm,
			current->pid, instrptr,
		        thumb_mode(regs) ? 4 : 8,
		        thumb_mode(regs) ? tinstr : instr,
		        addr, fsr);

	if (ai_usermode & 2)
		goto fixup;

	if (ai_usermode & 4)
		force_sig(SIGBUS, current);
	else
		set_cr(cr_no_alignment);

	return 0;
}

/*
 * This needs to be done after sysctl_init, otherwise sys/ will be
 * overwritten.  Actually, this shouldn't be in sys/ at all since
 * it isn't a sysctl, and it doesn't contain sysctl information.
 * We now locate it in /proc/cpu/alignment instead.
 */
static int __init alignment_init(void)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *res;

	res = proc_mkdir("cpu", NULL);
	if (!res)
		return -ENOMEM;

	res = create_proc_entry("alignment", S_IWUSR | S_IRUGO, res);
	if (!res)
		return -ENOMEM;

	res->read_proc = proc_alignment_read;
	res->write_proc = proc_alignment_write;
#endif

	hook_fault_code(1, do_alignment, SIGILL, "alignment exception");
	hook_fault_code(3, do_alignment, SIGILL, "alignment exception");

	return 0;
}

fs_initcall(alignment_init);
