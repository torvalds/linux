/* align.c - handle alignment exceptions for the Power PC.
 *
 * Copyright (c) 1996 Paul Mackerras <paulus@cs.anu.edu.au>
 * Copyright (c) 1998-1999 TiVo, Inc.
 *   PowerPC 403GCX modifications.
 * Copyright (c) 1999 Grant Erickson <grant@lcse.umn.edu>
 *   PowerPC 403GCX/405GP modifications.
 * Copyright (c) 2001-2002 PPC64 team, IBM Corp
 *   64-bit and Power4 support
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/cache.h>
#include <asm/cputable.h>

struct aligninfo {
	unsigned char len;
	unsigned char flags;
};

#define IS_XFORM(inst)	(((inst) >> 26) == 31)
#define IS_DSFORM(inst)	(((inst) >> 26) >= 56)

#define INVALID	{ 0, 0 }

#define LD	1	/* load */
#define ST	2	/* store */
#define	SE	4	/* sign-extend value */
#define F	8	/* to/from fp regs */
#define U	0x10	/* update index register */
#define M	0x20	/* multiple load/store */
#define SW	0x40	/* byte swap */

#define DCBZ	0x5f	/* 8xx/82xx dcbz faults when cache not enabled */

/*
 * The PowerPC stores certain bits of the instruction that caused the
 * alignment exception in the DSISR register.  This array maps those
 * bits to information about the operand length and what the
 * instruction would do.
 */
static struct aligninfo aligninfo[128] = {
	{ 4, LD },		/* 00 0 0000: lwz / lwarx */
	INVALID,		/* 00 0 0001 */
	{ 4, ST },		/* 00 0 0010: stw */
	INVALID,		/* 00 0 0011 */
	{ 2, LD },		/* 00 0 0100: lhz */
	{ 2, LD+SE },		/* 00 0 0101: lha */
	{ 2, ST },		/* 00 0 0110: sth */
	{ 4, LD+M },		/* 00 0 0111: lmw */
	{ 4, LD+F },		/* 00 0 1000: lfs */
	{ 8, LD+F },		/* 00 0 1001: lfd */
	{ 4, ST+F },		/* 00 0 1010: stfs */
	{ 8, ST+F },		/* 00 0 1011: stfd */
	INVALID,		/* 00 0 1100 */
	{ 8, LD },		/* 00 0 1101: ld */
	INVALID,		/* 00 0 1110 */
	{ 8, ST },		/* 00 0 1111: std */
	{ 4, LD+U },		/* 00 1 0000: lwzu */
	INVALID,		/* 00 1 0001 */
	{ 4, ST+U },		/* 00 1 0010: stwu */
	INVALID,		/* 00 1 0011 */
	{ 2, LD+U },		/* 00 1 0100: lhzu */
	{ 2, LD+SE+U },		/* 00 1 0101: lhau */
	{ 2, ST+U },		/* 00 1 0110: sthu */
	{ 4, ST+M },		/* 00 1 0111: stmw */
	{ 4, LD+F+U },		/* 00 1 1000: lfsu */
	{ 8, LD+F+U },		/* 00 1 1001: lfdu */
	{ 4, ST+F+U },		/* 00 1 1010: stfsu */
	{ 8, ST+F+U },		/* 00 1 1011: stfdu */
	INVALID,		/* 00 1 1100 */
	INVALID,		/* 00 1 1101 */
	INVALID,		/* 00 1 1110 */
	INVALID,		/* 00 1 1111 */
	{ 8, LD },		/* 01 0 0000: ldx */
	INVALID,		/* 01 0 0001 */
	{ 8, ST },		/* 01 0 0010: stdx */
	INVALID,		/* 01 0 0011 */
	INVALID,		/* 01 0 0100 */
	{ 4, LD+SE },		/* 01 0 0101: lwax */
	INVALID,		/* 01 0 0110 */
	INVALID,		/* 01 0 0111 */
	{ 0, LD },		/* 01 0 1000: lswx */
	{ 0, LD },		/* 01 0 1001: lswi */
	{ 0, ST },		/* 01 0 1010: stswx */
	{ 0, ST },		/* 01 0 1011: stswi */
	INVALID,		/* 01 0 1100 */
	{ 8, LD+U },		/* 01 0 1101: ldu */
	INVALID,		/* 01 0 1110 */
	{ 8, ST+U },		/* 01 0 1111: stdu */
	{ 8, LD+U },		/* 01 1 0000: ldux */
	INVALID,		/* 01 1 0001 */
	{ 8, ST+U },		/* 01 1 0010: stdux */
	INVALID,		/* 01 1 0011 */
	INVALID,		/* 01 1 0100 */
	{ 4, LD+SE+U },		/* 01 1 0101: lwaux */
	INVALID,		/* 01 1 0110 */
	INVALID,		/* 01 1 0111 */
	INVALID,		/* 01 1 1000 */
	INVALID,		/* 01 1 1001 */
	INVALID,		/* 01 1 1010 */
	INVALID,		/* 01 1 1011 */
	INVALID,		/* 01 1 1100 */
	INVALID,		/* 01 1 1101 */
	INVALID,		/* 01 1 1110 */
	INVALID,		/* 01 1 1111 */
	INVALID,		/* 10 0 0000 */
	INVALID,		/* 10 0 0001 */
	{ 0, ST },		/* 10 0 0010: stwcx. */
	INVALID,		/* 10 0 0011 */
	INVALID,		/* 10 0 0100 */
	INVALID,		/* 10 0 0101 */
	INVALID,		/* 10 0 0110 */
	INVALID,		/* 10 0 0111 */
	{ 4, LD+SW },		/* 10 0 1000: lwbrx */
	INVALID,		/* 10 0 1001 */
	{ 4, ST+SW },		/* 10 0 1010: stwbrx */
	INVALID,		/* 10 0 1011 */
	{ 2, LD+SW },		/* 10 0 1100: lhbrx */
	{ 4, LD+SE },		/* 10 0 1101  lwa */
	{ 2, ST+SW },		/* 10 0 1110: sthbrx */
	INVALID,		/* 10 0 1111 */
	INVALID,		/* 10 1 0000 */
	INVALID,		/* 10 1 0001 */
	INVALID,		/* 10 1 0010 */
	INVALID,		/* 10 1 0011 */
	INVALID,		/* 10 1 0100 */
	INVALID,		/* 10 1 0101 */
	INVALID,		/* 10 1 0110 */
	INVALID,		/* 10 1 0111 */
	INVALID,		/* 10 1 1000 */
	INVALID,		/* 10 1 1001 */
	INVALID,		/* 10 1 1010 */
	INVALID,		/* 10 1 1011 */
	INVALID,		/* 10 1 1100 */
	INVALID,		/* 10 1 1101 */
	INVALID,		/* 10 1 1110 */
	{ L1_CACHE_BYTES, ST },	/* 10 1 1111: dcbz */
	{ 4, LD },		/* 11 0 0000: lwzx */
	INVALID,		/* 11 0 0001 */
	{ 4, ST },		/* 11 0 0010: stwx */
	INVALID,		/* 11 0 0011 */
	{ 2, LD },		/* 11 0 0100: lhzx */
	{ 2, LD+SE },		/* 11 0 0101: lhax */
	{ 2, ST },		/* 11 0 0110: sthx */
	INVALID,		/* 11 0 0111 */
	{ 4, LD+F },		/* 11 0 1000: lfsx */
	{ 8, LD+F },		/* 11 0 1001: lfdx */
	{ 4, ST+F },		/* 11 0 1010: stfsx */
	{ 8, ST+F },		/* 11 0 1011: stfdx */
	INVALID,		/* 11 0 1100 */
	{ 8, LD+M },		/* 11 0 1101: lmd */
	INVALID,		/* 11 0 1110 */
	{ 8, ST+M },		/* 11 0 1111: stmd */
	{ 4, LD+U },		/* 11 1 0000: lwzux */
	INVALID,		/* 11 1 0001 */
	{ 4, ST+U },		/* 11 1 0010: stwux */
	INVALID,		/* 11 1 0011 */
	{ 2, LD+U },		/* 11 1 0100: lhzux */
	{ 2, LD+SE+U },		/* 11 1 0101: lhaux */
	{ 2, ST+U },		/* 11 1 0110: sthux */
	INVALID,		/* 11 1 0111 */
	{ 4, LD+F+U },		/* 11 1 1000: lfsux */
	{ 8, LD+F+U },		/* 11 1 1001: lfdux */
	{ 4, ST+F+U },		/* 11 1 1010: stfsux */
	{ 8, ST+F+U },		/* 11 1 1011: stfdux */
	INVALID,		/* 11 1 1100 */
	INVALID,		/* 11 1 1101 */
	INVALID,		/* 11 1 1110 */
	INVALID,		/* 11 1 1111 */
};

#define SWAP(a, b)	(t = (a), (a) = (b), (b) = t)

static inline unsigned make_dsisr(unsigned instr)
{
	unsigned dsisr;
	
	/* create a DSISR value from the instruction */
	dsisr = (instr & 0x03ff0000) >> 16;			/* bits  6:15 --> 22:31 */
	
	if ( IS_XFORM(instr) ) {
		dsisr |= (instr & 0x00000006) << 14;		/* bits 29:30 --> 15:16 */
		dsisr |= (instr & 0x00000040) << 8;		/* bit     25 -->    17 */
		dsisr |= (instr & 0x00000780) << 3;		/* bits 21:24 --> 18:21 */
	}
	else {
		dsisr |= (instr & 0x04000000) >> 12;		/* bit      5 -->    17 */
		dsisr |= (instr & 0x78000000) >> 17;		/* bits  1: 4 --> 18:21 */
		if ( IS_DSFORM(instr) ) {
			dsisr |= (instr & 0x00000003) << 18;	/* bits 30:31 --> 12:13 */
		}
	}
	
	return dsisr;
}

int
fix_alignment(struct pt_regs *regs)
{
	unsigned int instr, nb, flags;
	int t;
	unsigned long reg, areg;
	unsigned long i;
	int ret;
	unsigned dsisr;
	unsigned char __user *addr;
	unsigned char __user *p;
	unsigned long __user *lp;
	union {
		long ll;
		double dd;
		unsigned char v[8];
		struct {
			unsigned hi32;
			int	 low32;
		} x32;
		struct {
			unsigned char hi48[6];
			short	      low16;
		} x16;
	} data;

	/*
	 * Return 1 on success
	 * Return 0 if unable to handle the interrupt
	 * Return -EFAULT if data address is bad
	 */

	dsisr = regs->dsisr;

	if (cpu_has_feature(CPU_FTR_NODSISRALIGN)) {
	    unsigned int real_instr;
	    if (__get_user(real_instr, (unsigned int __user *)regs->nip))
		return 0;
	    dsisr = make_dsisr(real_instr);
	}

	/* extract the operation and registers from the dsisr */
	reg = (dsisr >> 5) & 0x1f;	/* source/dest register */
	areg = dsisr & 0x1f;		/* register to update */
	instr = (dsisr >> 10) & 0x7f;
	instr |= (dsisr >> 13) & 0x60;

	/* Lookup the operation in our table */
	nb = aligninfo[instr].len;
	flags = aligninfo[instr].flags;

	/* DAR has the operand effective address */
	addr = (unsigned char __user *)regs->dar;

	/* A size of 0 indicates an instruction we don't support */
	/* we also don't support the multiples (lmw, stmw, lmd, stmd) */
	if ((nb == 0) || (flags & M))
		return 0;		/* too hard or invalid instruction */

	/*
	 * Special handling for dcbz
	 * dcbz may give an alignment exception for accesses to caching inhibited
	 * storage
	 */
	if (instr == DCBZ)
		addr = (unsigned char __user *) ((unsigned long)addr & -L1_CACHE_BYTES);

	/* Verify the address of the operand */
	if (user_mode(regs)) {
		if (!access_ok((flags & ST? VERIFY_WRITE: VERIFY_READ), addr, nb))
			return -EFAULT;	/* bad address */
	}

	/* Force the fprs into the save area so we can reference them */
	if (flags & F) {
		if (!user_mode(regs))
			return 0;
		flush_fp_to_thread(current);
	}
	
	/* If we are loading, get the data from user space */
	if (flags & LD) {
		data.ll = 0;
		ret = 0;
		p = addr;
		switch (nb) {
		case 8:
			ret |= __get_user(data.v[0], p++);
			ret |= __get_user(data.v[1], p++);
			ret |= __get_user(data.v[2], p++);
			ret |= __get_user(data.v[3], p++);
		case 4:
			ret |= __get_user(data.v[4], p++);
			ret |= __get_user(data.v[5], p++);
		case 2:
			ret |= __get_user(data.v[6], p++);
			ret |= __get_user(data.v[7], p++);
			if (ret)
				return -EFAULT;
		}
	}
	
	/* If we are storing, get the data from the saved gpr or fpr */
	if (flags & ST) {
		if (flags & F) {
			if (nb == 4) {
				/* Doing stfs, have to convert to single */
				preempt_disable();
				enable_kernel_fp();
				cvt_df(&current->thread.fpr[reg], (float *)&data.v[4], &current->thread.fpscr);
				disable_kernel_fp();
				preempt_enable();
			}
			else
				data.dd = current->thread.fpr[reg];
		}
		else 
			data.ll = regs->gpr[reg];
	}
	
	/* Swap bytes as needed */
	if (flags & SW) {
		if (nb == 2)
			SWAP(data.v[6], data.v[7]);
		else {	/* nb must be 4 */
			SWAP(data.v[4], data.v[7]);
			SWAP(data.v[5], data.v[6]);
		}
	}
	
	/* Sign extend as needed */
	if (flags & SE) {
		if ( nb == 2 )
			data.ll = data.x16.low16;
		else	/* nb must be 4 */
			data.ll = data.x32.low32;
	}
	
	/* If we are loading, move the data to the gpr or fpr */
	if (flags & LD) {
		if (flags & F) {
			if (nb == 4) {
				/* Doing lfs, have to convert to double */
				preempt_disable();
				enable_kernel_fp();
				cvt_fd((float *)&data.v[4], &current->thread.fpr[reg], &current->thread.fpscr);
				disable_kernel_fp();
				preempt_enable();
			}
			else
				current->thread.fpr[reg] = data.dd;
		}
		else
			regs->gpr[reg] = data.ll;
	}
	
	/* If we are storing, copy the data to the user */
	if (flags & ST) {
		ret = 0;
		p = addr;
		switch (nb) {
		case 128:	/* Special case - must be dcbz */
			lp = (unsigned long __user *)p;
			for (i = 0; i < L1_CACHE_BYTES / sizeof(long); ++i)
				ret |= __put_user(0, lp++);
			break;
		case 8:
			ret |= __put_user(data.v[0], p++);
			ret |= __put_user(data.v[1], p++);
			ret |= __put_user(data.v[2], p++);
			ret |= __put_user(data.v[3], p++);
		case 4:
			ret |= __put_user(data.v[4], p++);
			ret |= __put_user(data.v[5], p++);
		case 2:
			ret |= __put_user(data.v[6], p++);
			ret |= __put_user(data.v[7], p++);
		}
		if (ret)
			return -EFAULT;
	}
	
	/* Update RA as needed */
	if (flags & U) {
		regs->gpr[areg] = regs->dar;
	}

	return 1;
}

