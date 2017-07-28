/*
 * Single-step support.
 *
 * Copyright (C) 2004 Paul Mackerras <paulus@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>
#include <linux/prefetch.h>
#include <asm/sstep.h>
#include <asm/processor.h>
#include <asm/uaccess.h>
#include <asm/cputable.h>

extern char system_call_common[];

#ifdef CONFIG_PPC64
/* Bits in SRR1 that are copied from MSR */
#define MSR_MASK	0xffffffff87c0ffffUL
#else
#define MSR_MASK	0x87c0ffff
#endif

/* Bits in XER */
#define XER_SO		0x80000000U
#define XER_OV		0x40000000U
#define XER_CA		0x20000000U

#ifdef CONFIG_PPC_FPU
/*
 * Functions in ldstfp.S
 */
extern int do_lfs(int rn, unsigned long ea);
extern int do_lfd(int rn, unsigned long ea);
extern int do_stfs(int rn, unsigned long ea);
extern int do_stfd(int rn, unsigned long ea);
extern int do_lvx(int rn, unsigned long ea);
extern int do_stvx(int rn, unsigned long ea);
extern int do_lxvd2x(int rn, unsigned long ea);
extern int do_stxvd2x(int rn, unsigned long ea);
#endif

/*
 * Emulate the truncation of 64 bit values in 32-bit mode.
 */
static unsigned long truncate_if_32bit(unsigned long msr, unsigned long val)
{
#ifdef __powerpc64__
	if ((msr & MSR_64BIT) == 0)
		val &= 0xffffffffUL;
#endif
	return val;
}

/*
 * Determine whether a conditional branch instruction would branch.
 */
static int __kprobes branch_taken(unsigned int instr, struct pt_regs *regs)
{
	unsigned int bo = (instr >> 21) & 0x1f;
	unsigned int bi;

	if ((bo & 4) == 0) {
		/* decrement counter */
		--regs->ctr;
		if (((bo >> 1) & 1) ^ (regs->ctr == 0))
			return 0;
	}
	if ((bo & 0x10) == 0) {
		/* check bit from CR */
		bi = (instr >> 16) & 0x1f;
		if (((regs->ccr >> (31 - bi)) & 1) != ((bo >> 3) & 1))
			return 0;
	}
	return 1;
}


static long __kprobes address_ok(struct pt_regs *regs, unsigned long ea, int nb)
{
	if (!user_mode(regs))
		return 1;
	return __access_ok(ea, nb, USER_DS);
}

/*
 * Calculate effective address for a D-form instruction
 */
static unsigned long __kprobes dform_ea(unsigned int instr, struct pt_regs *regs)
{
	int ra;
	unsigned long ea;

	ra = (instr >> 16) & 0x1f;
	ea = (signed short) instr;		/* sign-extend */
	if (ra)
		ea += regs->gpr[ra];

	return truncate_if_32bit(regs->msr, ea);
}

#ifdef __powerpc64__
/*
 * Calculate effective address for a DS-form instruction
 */
static unsigned long __kprobes dsform_ea(unsigned int instr, struct pt_regs *regs)
{
	int ra;
	unsigned long ea;

	ra = (instr >> 16) & 0x1f;
	ea = (signed short) (instr & ~3);	/* sign-extend */
	if (ra)
		ea += regs->gpr[ra];

	return truncate_if_32bit(regs->msr, ea);
}
#endif /* __powerpc64 */

/*
 * Calculate effective address for an X-form instruction
 */
static unsigned long __kprobes xform_ea(unsigned int instr,
					struct pt_regs *regs)
{
	int ra, rb;
	unsigned long ea;

	ra = (instr >> 16) & 0x1f;
	rb = (instr >> 11) & 0x1f;
	ea = regs->gpr[rb];
	if (ra)
		ea += regs->gpr[ra];

	return truncate_if_32bit(regs->msr, ea);
}

/*
 * Return the largest power of 2, not greater than sizeof(unsigned long),
 * such that x is a multiple of it.
 */
static inline unsigned long max_align(unsigned long x)
{
	x |= sizeof(unsigned long);
	return x & -x;		/* isolates rightmost bit */
}


static inline unsigned long byterev_2(unsigned long x)
{
	return ((x >> 8) & 0xff) | ((x & 0xff) << 8);
}

static inline unsigned long byterev_4(unsigned long x)
{
	return ((x >> 24) & 0xff) | ((x >> 8) & 0xff00) |
		((x & 0xff00) << 8) | ((x & 0xff) << 24);
}

#ifdef __powerpc64__
static inline unsigned long byterev_8(unsigned long x)
{
	return (byterev_4(x) << 32) | byterev_4(x >> 32);
}
#endif

static int __kprobes read_mem_aligned(unsigned long *dest, unsigned long ea,
				      int nb)
{
	int err = 0;
	unsigned long x = 0;

	switch (nb) {
	case 1:
		err = __get_user(x, (unsigned char __user *) ea);
		break;
	case 2:
		err = __get_user(x, (unsigned short __user *) ea);
		break;
	case 4:
		err = __get_user(x, (unsigned int __user *) ea);
		break;
#ifdef __powerpc64__
	case 8:
		err = __get_user(x, (unsigned long __user *) ea);
		break;
#endif
	}
	if (!err)
		*dest = x;
	return err;
}

static int __kprobes read_mem_unaligned(unsigned long *dest, unsigned long ea,
					int nb, struct pt_regs *regs)
{
	int err;
	unsigned long x, b, c;
#ifdef __LITTLE_ENDIAN__
	int len = nb; /* save a copy of the length for byte reversal */
#endif

	/* unaligned, do this in pieces */
	x = 0;
	for (; nb > 0; nb -= c) {
#ifdef __LITTLE_ENDIAN__
		c = 1;
#endif
#ifdef __BIG_ENDIAN__
		c = max_align(ea);
#endif
		if (c > nb)
			c = max_align(nb);
		err = read_mem_aligned(&b, ea, c);
		if (err)
			return err;
		x = (x << (8 * c)) + b;
		ea += c;
	}
#ifdef __LITTLE_ENDIAN__
	switch (len) {
	case 2:
		*dest = byterev_2(x);
		break;
	case 4:
		*dest = byterev_4(x);
		break;
#ifdef __powerpc64__
	case 8:
		*dest = byterev_8(x);
		break;
#endif
	}
#endif
#ifdef __BIG_ENDIAN__
	*dest = x;
#endif
	return 0;
}

/*
 * Read memory at address ea for nb bytes, return 0 for success
 * or -EFAULT if an error occurred.
 */
static int __kprobes read_mem(unsigned long *dest, unsigned long ea, int nb,
			      struct pt_regs *regs)
{
	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	if ((ea & (nb - 1)) == 0)
		return read_mem_aligned(dest, ea, nb);
	return read_mem_unaligned(dest, ea, nb, regs);
}

static int __kprobes write_mem_aligned(unsigned long val, unsigned long ea,
				       int nb)
{
	int err = 0;

	switch (nb) {
	case 1:
		err = __put_user(val, (unsigned char __user *) ea);
		break;
	case 2:
		err = __put_user(val, (unsigned short __user *) ea);
		break;
	case 4:
		err = __put_user(val, (unsigned int __user *) ea);
		break;
#ifdef __powerpc64__
	case 8:
		err = __put_user(val, (unsigned long __user *) ea);
		break;
#endif
	}
	return err;
}

static int __kprobes write_mem_unaligned(unsigned long val, unsigned long ea,
					 int nb, struct pt_regs *regs)
{
	int err;
	unsigned long c;

#ifdef __LITTLE_ENDIAN__
	switch (nb) {
	case 2:
		val = byterev_2(val);
		break;
	case 4:
		val = byterev_4(val);
		break;
#ifdef __powerpc64__
	case 8:
		val = byterev_8(val);
		break;
#endif
	}
#endif
	/* unaligned or little-endian, do this in pieces */
	for (; nb > 0; nb -= c) {
#ifdef __LITTLE_ENDIAN__
		c = 1;
#endif
#ifdef __BIG_ENDIAN__
		c = max_align(ea);
#endif
		if (c > nb)
			c = max_align(nb);
		err = write_mem_aligned(val >> (nb - c) * 8, ea, c);
		if (err)
			return err;
		ea += c;
	}
	return 0;
}

/*
 * Write memory at address ea for nb bytes, return 0 for success
 * or -EFAULT if an error occurred.
 */
static int __kprobes write_mem(unsigned long val, unsigned long ea, int nb,
			       struct pt_regs *regs)
{
	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	if ((ea & (nb - 1)) == 0)
		return write_mem_aligned(val, ea, nb);
	return write_mem_unaligned(val, ea, nb, regs);
}

#ifdef CONFIG_PPC_FPU
/*
 * Check the address and alignment, and call func to do the actual
 * load or store.
 */
static int __kprobes do_fp_load(int rn, int (*func)(int, unsigned long),
				unsigned long ea, int nb,
				struct pt_regs *regs)
{
	int err;
	union {
		double dbl;
		unsigned long ul[2];
		struct {
#ifdef __BIG_ENDIAN__
			unsigned _pad_;
			unsigned word;
#endif
#ifdef __LITTLE_ENDIAN__
			unsigned word;
			unsigned _pad_;
#endif
		} single;
	} data;
	unsigned long ptr;

	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	if ((ea & 3) == 0)
		return (*func)(rn, ea);
	ptr = (unsigned long) &data.ul;
	if (sizeof(unsigned long) == 8 || nb == 4) {
		err = read_mem_unaligned(&data.ul[0], ea, nb, regs);
		if (nb == 4)
			ptr = (unsigned long)&(data.single.word);
	} else {
		/* reading a double on 32-bit */
		err = read_mem_unaligned(&data.ul[0], ea, 4, regs);
		if (!err)
			err = read_mem_unaligned(&data.ul[1], ea + 4, 4, regs);
	}
	if (err)
		return err;
	return (*func)(rn, ptr);
}

static int __kprobes do_fp_store(int rn, int (*func)(int, unsigned long),
				 unsigned long ea, int nb,
				 struct pt_regs *regs)
{
	int err;
	union {
		double dbl;
		unsigned long ul[2];
		struct {
#ifdef __BIG_ENDIAN__
			unsigned _pad_;
			unsigned word;
#endif
#ifdef __LITTLE_ENDIAN__
			unsigned word;
			unsigned _pad_;
#endif
		} single;
	} data;
	unsigned long ptr;

	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	if ((ea & 3) == 0)
		return (*func)(rn, ea);
	ptr = (unsigned long) &data.ul[0];
	if (sizeof(unsigned long) == 8 || nb == 4) {
		if (nb == 4)
			ptr = (unsigned long)&(data.single.word);
		err = (*func)(rn, ptr);
		if (err)
			return err;
		err = write_mem_unaligned(data.ul[0], ea, nb, regs);
	} else {
		/* writing a double on 32-bit */
		err = (*func)(rn, ptr);
		if (err)
			return err;
		err = write_mem_unaligned(data.ul[0], ea, 4, regs);
		if (!err)
			err = write_mem_unaligned(data.ul[1], ea + 4, 4, regs);
	}
	return err;
}
#endif

#ifdef CONFIG_ALTIVEC
/* For Altivec/VMX, no need to worry about alignment */
static int __kprobes do_vec_load(int rn, int (*func)(int, unsigned long),
				 unsigned long ea, struct pt_regs *regs)
{
	if (!address_ok(regs, ea & ~0xfUL, 16))
		return -EFAULT;
	return (*func)(rn, ea);
}

static int __kprobes do_vec_store(int rn, int (*func)(int, unsigned long),
				  unsigned long ea, struct pt_regs *regs)
{
	if (!address_ok(regs, ea & ~0xfUL, 16))
		return -EFAULT;
	return (*func)(rn, ea);
}
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_VSX
static int __kprobes do_vsx_load(int rn, int (*func)(int, unsigned long),
				 unsigned long ea, struct pt_regs *regs)
{
	int err;
	unsigned long val[2];

	if (!address_ok(regs, ea, 16))
		return -EFAULT;
	if ((ea & 3) == 0)
		return (*func)(rn, ea);
	err = read_mem_unaligned(&val[0], ea, 8, regs);
	if (!err)
		err = read_mem_unaligned(&val[1], ea + 8, 8, regs);
	if (!err)
		err = (*func)(rn, (unsigned long) &val[0]);
	return err;
}

static int __kprobes do_vsx_store(int rn, int (*func)(int, unsigned long),
				 unsigned long ea, struct pt_regs *regs)
{
	int err;
	unsigned long val[2];

	if (!address_ok(regs, ea, 16))
		return -EFAULT;
	if ((ea & 3) == 0)
		return (*func)(rn, ea);
	err = (*func)(rn, (unsigned long) &val[0]);
	if (err)
		return err;
	err = write_mem_unaligned(val[0], ea, 8, regs);
	if (!err)
		err = write_mem_unaligned(val[1], ea + 8, 8, regs);
	return err;
}
#endif /* CONFIG_VSX */

#define __put_user_asmx(x, addr, err, op, cr)		\
	__asm__ __volatile__(				\
		"1:	" op " %2,0,%3\n"		\
		"	mfcr	%1\n"			\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li	%0,%4\n"		\
		"	b	2b\n"			\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
			PPC_LONG_ALIGN "\n"		\
			PPC_LONG "1b,3b\n"		\
		".previous"				\
		: "=r" (err), "=r" (cr)			\
		: "r" (x), "r" (addr), "i" (-EFAULT), "0" (err))

#define __get_user_asmx(x, addr, err, op)		\
	__asm__ __volatile__(				\
		"1:	"op" %1,0,%2\n"			\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li	%0,%3\n"		\
		"	b	2b\n"			\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
			PPC_LONG_ALIGN "\n"		\
			PPC_LONG "1b,3b\n"		\
		".previous"				\
		: "=r" (err), "=r" (x)			\
		: "r" (addr), "i" (-EFAULT), "0" (err))

#define __cacheop_user_asmx(addr, err, op)		\
	__asm__ __volatile__(				\
		"1:	"op" 0,%1\n"			\
		"2:\n"					\
		".section .fixup,\"ax\"\n"		\
		"3:	li	%0,%3\n"		\
		"	b	2b\n"			\
		".previous\n"				\
		".section __ex_table,\"a\"\n"		\
			PPC_LONG_ALIGN "\n"		\
			PPC_LONG "1b,3b\n"		\
		".previous"				\
		: "=r" (err)				\
		: "r" (addr), "i" (-EFAULT), "0" (err))

static void __kprobes set_cr0(struct pt_regs *regs, int rd)
{
	long val = regs->gpr[rd];

	regs->ccr = (regs->ccr & 0x0fffffff) | ((regs->xer >> 3) & 0x10000000);
#ifdef __powerpc64__
	if (!(regs->msr & MSR_64BIT))
		val = (int) val;
#endif
	if (val < 0)
		regs->ccr |= 0x80000000;
	else if (val > 0)
		regs->ccr |= 0x40000000;
	else
		regs->ccr |= 0x20000000;
}

static void __kprobes add_with_carry(struct pt_regs *regs, int rd,
				     unsigned long val1, unsigned long val2,
				     unsigned long carry_in)
{
	unsigned long val = val1 + val2;

	if (carry_in)
		++val;
	regs->gpr[rd] = val;
#ifdef __powerpc64__
	if (!(regs->msr & MSR_64BIT)) {
		val = (unsigned int) val;
		val1 = (unsigned int) val1;
	}
#endif
	if (val < val1 || (carry_in && val == val1))
		regs->xer |= XER_CA;
	else
		regs->xer &= ~XER_CA;
}

static void __kprobes do_cmp_signed(struct pt_regs *regs, long v1, long v2,
				    int crfld)
{
	unsigned int crval, shift;

	crval = (regs->xer >> 31) & 1;		/* get SO bit */
	if (v1 < v2)
		crval |= 8;
	else if (v1 > v2)
		crval |= 4;
	else
		crval |= 2;
	shift = (7 - crfld) * 4;
	regs->ccr = (regs->ccr & ~(0xf << shift)) | (crval << shift);
}

static void __kprobes do_cmp_unsigned(struct pt_regs *regs, unsigned long v1,
				      unsigned long v2, int crfld)
{
	unsigned int crval, shift;

	crval = (regs->xer >> 31) & 1;		/* get SO bit */
	if (v1 < v2)
		crval |= 8;
	else if (v1 > v2)
		crval |= 4;
	else
		crval |= 2;
	shift = (7 - crfld) * 4;
	regs->ccr = (regs->ccr & ~(0xf << shift)) | (crval << shift);
}

static int __kprobes trap_compare(long v1, long v2)
{
	int ret = 0;

	if (v1 < v2)
		ret |= 0x10;
	else if (v1 > v2)
		ret |= 0x08;
	else
		ret |= 0x04;
	if ((unsigned long)v1 < (unsigned long)v2)
		ret |= 0x02;
	else if ((unsigned long)v1 > (unsigned long)v2)
		ret |= 0x01;
	return ret;
}

/*
 * Elements of 32-bit rotate and mask instructions.
 */
#define MASK32(mb, me)	((0xffffffffUL >> (mb)) + \
			 ((signed long)-0x80000000L >> (me)) + ((me) >= (mb)))
#ifdef __powerpc64__
#define MASK64_L(mb)	(~0UL >> (mb))
#define MASK64_R(me)	((signed long)-0x8000000000000000L >> (me))
#define MASK64(mb, me)	(MASK64_L(mb) + MASK64_R(me) + ((me) >= (mb)))
#define DATA32(x)	(((x) & 0xffffffffUL) | (((x) & 0xffffffffUL) << 32))
#else
#define DATA32(x)	(x)
#endif
#define ROTATE(x, n)	((n) ? (((x) << (n)) | ((x) >> (8 * sizeof(long) - (n)))) : (x))

/*
 * Decode an instruction, and execute it if that can be done just by
 * modifying *regs (i.e. integer arithmetic and logical instructions,
 * branches, and barrier instructions).
 * Returns 1 if the instruction has been executed, or 0 if not.
 * Sets *op to indicate what the instruction does.
 */
int __kprobes analyse_instr(struct instruction_op *op, struct pt_regs *regs,
			    unsigned int instr)
{
	unsigned int opcode, ra, rb, rd, spr, u;
	unsigned long int imm;
	unsigned long int val, val2;
	unsigned int mb, me, sh;
	long ival;

	op->type = COMPUTE;

	opcode = instr >> 26;
	switch (opcode) {
	case 16:	/* bc */
		op->type = BRANCH;
		imm = (signed short)(instr & 0xfffc);
		if ((instr & 2) == 0)
			imm += regs->nip;
		regs->nip += 4;
		regs->nip = truncate_if_32bit(regs->msr, regs->nip);
		if (instr & 1)
			regs->link = regs->nip;
		if (branch_taken(instr, regs))
			regs->nip = truncate_if_32bit(regs->msr, imm);
		return 1;
#ifdef CONFIG_PPC64
	case 17:	/* sc */
		if ((instr & 0xfe2) == 2)
			op->type = SYSCALL;
		else
			op->type = UNKNOWN;
		return 0;
#endif
	case 18:	/* b */
		op->type = BRANCH;
		imm = instr & 0x03fffffc;
		if (imm & 0x02000000)
			imm -= 0x04000000;
		if ((instr & 2) == 0)
			imm += regs->nip;
		if (instr & 1)
			regs->link = truncate_if_32bit(regs->msr, regs->nip + 4);
		imm = truncate_if_32bit(regs->msr, imm);
		regs->nip = imm;
		return 1;
	case 19:
		switch ((instr >> 1) & 0x3ff) {
		case 0:		/* mcrf */
			rd = 7 - ((instr >> 23) & 0x7);
			ra = 7 - ((instr >> 18) & 0x7);
			rd *= 4;
			ra *= 4;
			val = (regs->ccr >> ra) & 0xf;
			regs->ccr = (regs->ccr & ~(0xfUL << rd)) | (val << rd);
			goto instr_done;

		case 16:	/* bclr */
		case 528:	/* bcctr */
			op->type = BRANCH;
			imm = (instr & 0x400)? regs->ctr: regs->link;
			regs->nip = truncate_if_32bit(regs->msr, regs->nip + 4);
			imm = truncate_if_32bit(regs->msr, imm);
			if (instr & 1)
				regs->link = regs->nip;
			if (branch_taken(instr, regs))
				regs->nip = imm;
			return 1;

		case 18:	/* rfid, scary */
			if (regs->msr & MSR_PR)
				goto priv;
			op->type = RFI;
			return 0;

		case 150:	/* isync */
			op->type = BARRIER;
			isync();
			goto instr_done;

		case 33:	/* crnor */
		case 129:	/* crandc */
		case 193:	/* crxor */
		case 225:	/* crnand */
		case 257:	/* crand */
		case 289:	/* creqv */
		case 417:	/* crorc */
		case 449:	/* cror */
			ra = (instr >> 16) & 0x1f;
			rb = (instr >> 11) & 0x1f;
			rd = (instr >> 21) & 0x1f;
			ra = (regs->ccr >> (31 - ra)) & 1;
			rb = (regs->ccr >> (31 - rb)) & 1;
			val = (instr >> (6 + ra * 2 + rb)) & 1;
			regs->ccr = (regs->ccr & ~(1UL << (31 - rd))) |
				(val << (31 - rd));
			goto instr_done;
		}
		break;
	case 31:
		switch ((instr >> 1) & 0x3ff) {
		case 598:	/* sync */
			op->type = BARRIER;
#ifdef __powerpc64__
			switch ((instr >> 21) & 3) {
			case 1:		/* lwsync */
				asm volatile("lwsync" : : : "memory");
				goto instr_done;
			case 2:		/* ptesync */
				asm volatile("ptesync" : : : "memory");
				goto instr_done;
			}
#endif
			mb();
			goto instr_done;

		case 854:	/* eieio */
			op->type = BARRIER;
			eieio();
			goto instr_done;
		}
		break;
	}

	/* Following cases refer to regs->gpr[], so we need all regs */
	if (!FULL_REGS(regs))
		return 0;

	rd = (instr >> 21) & 0x1f;
	ra = (instr >> 16) & 0x1f;
	rb = (instr >> 11) & 0x1f;

	switch (opcode) {
#ifdef __powerpc64__
	case 2:		/* tdi */
		if (rd & trap_compare(regs->gpr[ra], (short) instr))
			goto trap;
		goto instr_done;
#endif
	case 3:		/* twi */
		if (rd & trap_compare((int)regs->gpr[ra], (short) instr))
			goto trap;
		goto instr_done;

	case 7:		/* mulli */
		regs->gpr[rd] = regs->gpr[ra] * (short) instr;
		goto instr_done;

	case 8:		/* subfic */
		imm = (short) instr;
		add_with_carry(regs, rd, ~regs->gpr[ra], imm, 1);
		goto instr_done;

	case 10:	/* cmpli */
		imm = (unsigned short) instr;
		val = regs->gpr[ra];
#ifdef __powerpc64__
		if ((rd & 1) == 0)
			val = (unsigned int) val;
#endif
		do_cmp_unsigned(regs, val, imm, rd >> 2);
		goto instr_done;

	case 11:	/* cmpi */
		imm = (short) instr;
		val = regs->gpr[ra];
#ifdef __powerpc64__
		if ((rd & 1) == 0)
			val = (int) val;
#endif
		do_cmp_signed(regs, val, imm, rd >> 2);
		goto instr_done;

	case 12:	/* addic */
		imm = (short) instr;
		add_with_carry(regs, rd, regs->gpr[ra], imm, 0);
		goto instr_done;

	case 13:	/* addic. */
		imm = (short) instr;
		add_with_carry(regs, rd, regs->gpr[ra], imm, 0);
		set_cr0(regs, rd);
		goto instr_done;

	case 14:	/* addi */
		imm = (short) instr;
		if (ra)
			imm += regs->gpr[ra];
		regs->gpr[rd] = imm;
		goto instr_done;

	case 15:	/* addis */
		imm = ((short) instr) << 16;
		if (ra)
			imm += regs->gpr[ra];
		regs->gpr[rd] = imm;
		goto instr_done;

	case 20:	/* rlwimi */
		mb = (instr >> 6) & 0x1f;
		me = (instr >> 1) & 0x1f;
		val = DATA32(regs->gpr[rd]);
		imm = MASK32(mb, me);
		regs->gpr[ra] = (regs->gpr[ra] & ~imm) | (ROTATE(val, rb) & imm);
		goto logical_done;

	case 21:	/* rlwinm */
		mb = (instr >> 6) & 0x1f;
		me = (instr >> 1) & 0x1f;
		val = DATA32(regs->gpr[rd]);
		regs->gpr[ra] = ROTATE(val, rb) & MASK32(mb, me);
		goto logical_done;

	case 23:	/* rlwnm */
		mb = (instr >> 6) & 0x1f;
		me = (instr >> 1) & 0x1f;
		rb = regs->gpr[rb] & 0x1f;
		val = DATA32(regs->gpr[rd]);
		regs->gpr[ra] = ROTATE(val, rb) & MASK32(mb, me);
		goto logical_done;

	case 24:	/* ori */
		imm = (unsigned short) instr;
		regs->gpr[ra] = regs->gpr[rd] | imm;
		goto instr_done;

	case 25:	/* oris */
		imm = (unsigned short) instr;
		regs->gpr[ra] = regs->gpr[rd] | (imm << 16);
		goto instr_done;

	case 26:	/* xori */
		imm = (unsigned short) instr;
		regs->gpr[ra] = regs->gpr[rd] ^ imm;
		goto instr_done;

	case 27:	/* xoris */
		imm = (unsigned short) instr;
		regs->gpr[ra] = regs->gpr[rd] ^ (imm << 16);
		goto instr_done;

	case 28:	/* andi. */
		imm = (unsigned short) instr;
		regs->gpr[ra] = regs->gpr[rd] & imm;
		set_cr0(regs, ra);
		goto instr_done;

	case 29:	/* andis. */
		imm = (unsigned short) instr;
		regs->gpr[ra] = regs->gpr[rd] & (imm << 16);
		set_cr0(regs, ra);
		goto instr_done;

#ifdef __powerpc64__
	case 30:	/* rld* */
		mb = ((instr >> 6) & 0x1f) | (instr & 0x20);
		val = regs->gpr[rd];
		if ((instr & 0x10) == 0) {
			sh = rb | ((instr & 2) << 4);
			val = ROTATE(val, sh);
			switch ((instr >> 2) & 3) {
			case 0:		/* rldicl */
				regs->gpr[ra] = val & MASK64_L(mb);
				goto logical_done;
			case 1:		/* rldicr */
				regs->gpr[ra] = val & MASK64_R(mb);
				goto logical_done;
			case 2:		/* rldic */
				regs->gpr[ra] = val & MASK64(mb, 63 - sh);
				goto logical_done;
			case 3:		/* rldimi */
				imm = MASK64(mb, 63 - sh);
				regs->gpr[ra] = (regs->gpr[ra] & ~imm) |
					(val & imm);
				goto logical_done;
			}
		} else {
			sh = regs->gpr[rb] & 0x3f;
			val = ROTATE(val, sh);
			switch ((instr >> 1) & 7) {
			case 0:		/* rldcl */
				regs->gpr[ra] = val & MASK64_L(mb);
				goto logical_done;
			case 1:		/* rldcr */
				regs->gpr[ra] = val & MASK64_R(mb);
				goto logical_done;
			}
		}
#endif

	case 31:
		switch ((instr >> 1) & 0x3ff) {
		case 4:		/* tw */
			if (rd == 0x1f ||
			    (rd & trap_compare((int)regs->gpr[ra],
					       (int)regs->gpr[rb])))
				goto trap;
			goto instr_done;
#ifdef __powerpc64__
		case 68:	/* td */
			if (rd & trap_compare(regs->gpr[ra], regs->gpr[rb]))
				goto trap;
			goto instr_done;
#endif
		case 83:	/* mfmsr */
			if (regs->msr & MSR_PR)
				goto priv;
			op->type = MFMSR;
			op->reg = rd;
			return 0;
		case 146:	/* mtmsr */
			if (regs->msr & MSR_PR)
				goto priv;
			op->type = MTMSR;
			op->reg = rd;
			op->val = 0xffffffff & ~(MSR_ME | MSR_LE);
			return 0;
#ifdef CONFIG_PPC64
		case 178:	/* mtmsrd */
			if (regs->msr & MSR_PR)
				goto priv;
			op->type = MTMSR;
			op->reg = rd;
			/* only MSR_EE and MSR_RI get changed if bit 15 set */
			/* mtmsrd doesn't change MSR_HV, MSR_ME or MSR_LE */
			imm = (instr & 0x10000)? 0x8002: 0xefffffffffffeffeUL;
			op->val = imm;
			return 0;
#endif

		case 19:	/* mfcr */
			if ((instr >> 20) & 1) {
				imm = 0xf0000000UL;
				for (sh = 0; sh < 8; ++sh) {
					if (instr & (0x80000 >> sh)) {
						regs->gpr[rd] = regs->ccr & imm;
						break;
					}
					imm >>= 4;
				}

				goto instr_done;
			}

			regs->gpr[rd] = regs->ccr;
			regs->gpr[rd] &= 0xffffffffUL;
			goto instr_done;

		case 144:	/* mtcrf */
			imm = 0xf0000000UL;
			val = regs->gpr[rd];
			for (sh = 0; sh < 8; ++sh) {
				if (instr & (0x80000 >> sh))
					regs->ccr = (regs->ccr & ~imm) |
						(val & imm);
				imm >>= 4;
			}
			goto instr_done;

		case 339:	/* mfspr */
			spr = ((instr >> 16) & 0x1f) | ((instr >> 6) & 0x3e0);
			switch (spr) {
			case SPRN_XER:	/* mfxer */
				regs->gpr[rd] = regs->xer;
				regs->gpr[rd] &= 0xffffffffUL;
				goto instr_done;
			case SPRN_LR:	/* mflr */
				regs->gpr[rd] = regs->link;
				goto instr_done;
			case SPRN_CTR:	/* mfctr */
				regs->gpr[rd] = regs->ctr;
				goto instr_done;
			default:
				op->type = MFSPR;
				op->reg = rd;
				op->spr = spr;
				return 0;
			}
			break;

		case 467:	/* mtspr */
			spr = ((instr >> 16) & 0x1f) | ((instr >> 6) & 0x3e0);
			switch (spr) {
			case SPRN_XER:	/* mtxer */
				regs->xer = (regs->gpr[rd] & 0xffffffffUL);
				goto instr_done;
			case SPRN_LR:	/* mtlr */
				regs->link = regs->gpr[rd];
				goto instr_done;
			case SPRN_CTR:	/* mtctr */
				regs->ctr = regs->gpr[rd];
				goto instr_done;
			default:
				op->type = MTSPR;
				op->val = regs->gpr[rd];
				op->spr = spr;
				return 0;
			}
			break;

/*
 * Compare instructions
 */
		case 0:	/* cmp */
			val = regs->gpr[ra];
			val2 = regs->gpr[rb];
#ifdef __powerpc64__
			if ((rd & 1) == 0) {
				/* word (32-bit) compare */
				val = (int) val;
				val2 = (int) val2;
			}
#endif
			do_cmp_signed(regs, val, val2, rd >> 2);
			goto instr_done;

		case 32:	/* cmpl */
			val = regs->gpr[ra];
			val2 = regs->gpr[rb];
#ifdef __powerpc64__
			if ((rd & 1) == 0) {
				/* word (32-bit) compare */
				val = (unsigned int) val;
				val2 = (unsigned int) val2;
			}
#endif
			do_cmp_unsigned(regs, val, val2, rd >> 2);
			goto instr_done;

/*
 * Arithmetic instructions
 */
		case 8:	/* subfc */
			add_with_carry(regs, rd, ~regs->gpr[ra],
				       regs->gpr[rb], 1);
			goto arith_done;
#ifdef __powerpc64__
		case 9:	/* mulhdu */
			asm("mulhdu %0,%1,%2" : "=r" (regs->gpr[rd]) :
			    "r" (regs->gpr[ra]), "r" (regs->gpr[rb]));
			goto arith_done;
#endif
		case 10:	/* addc */
			add_with_carry(regs, rd, regs->gpr[ra],
				       regs->gpr[rb], 0);
			goto arith_done;

		case 11:	/* mulhwu */
			asm("mulhwu %0,%1,%2" : "=r" (regs->gpr[rd]) :
			    "r" (regs->gpr[ra]), "r" (regs->gpr[rb]));
			goto arith_done;

		case 40:	/* subf */
			regs->gpr[rd] = regs->gpr[rb] - regs->gpr[ra];
			goto arith_done;
#ifdef __powerpc64__
		case 73:	/* mulhd */
			asm("mulhd %0,%1,%2" : "=r" (regs->gpr[rd]) :
			    "r" (regs->gpr[ra]), "r" (regs->gpr[rb]));
			goto arith_done;
#endif
		case 75:	/* mulhw */
			asm("mulhw %0,%1,%2" : "=r" (regs->gpr[rd]) :
			    "r" (regs->gpr[ra]), "r" (regs->gpr[rb]));
			goto arith_done;

		case 104:	/* neg */
			regs->gpr[rd] = -regs->gpr[ra];
			goto arith_done;

		case 136:	/* subfe */
			add_with_carry(regs, rd, ~regs->gpr[ra], regs->gpr[rb],
				       regs->xer & XER_CA);
			goto arith_done;

		case 138:	/* adde */
			add_with_carry(regs, rd, regs->gpr[ra], regs->gpr[rb],
				       regs->xer & XER_CA);
			goto arith_done;

		case 200:	/* subfze */
			add_with_carry(regs, rd, ~regs->gpr[ra], 0L,
				       regs->xer & XER_CA);
			goto arith_done;

		case 202:	/* addze */
			add_with_carry(regs, rd, regs->gpr[ra], 0L,
				       regs->xer & XER_CA);
			goto arith_done;

		case 232:	/* subfme */
			add_with_carry(regs, rd, ~regs->gpr[ra], -1L,
				       regs->xer & XER_CA);
			goto arith_done;
#ifdef __powerpc64__
		case 233:	/* mulld */
			regs->gpr[rd] = regs->gpr[ra] * regs->gpr[rb];
			goto arith_done;
#endif
		case 234:	/* addme */
			add_with_carry(regs, rd, regs->gpr[ra], -1L,
				       regs->xer & XER_CA);
			goto arith_done;

		case 235:	/* mullw */
			regs->gpr[rd] = (unsigned int) regs->gpr[ra] *
				(unsigned int) regs->gpr[rb];
			goto arith_done;

		case 266:	/* add */
			regs->gpr[rd] = regs->gpr[ra] + regs->gpr[rb];
			goto arith_done;
#ifdef __powerpc64__
		case 457:	/* divdu */
			regs->gpr[rd] = regs->gpr[ra] / regs->gpr[rb];
			goto arith_done;
#endif
		case 459:	/* divwu */
			regs->gpr[rd] = (unsigned int) regs->gpr[ra] /
				(unsigned int) regs->gpr[rb];
			goto arith_done;
#ifdef __powerpc64__
		case 489:	/* divd */
			regs->gpr[rd] = (long int) regs->gpr[ra] /
				(long int) regs->gpr[rb];
			goto arith_done;
#endif
		case 491:	/* divw */
			regs->gpr[rd] = (int) regs->gpr[ra] /
				(int) regs->gpr[rb];
			goto arith_done;


/*
 * Logical instructions
 */
		case 26:	/* cntlzw */
			asm("cntlzw %0,%1" : "=r" (regs->gpr[ra]) :
			    "r" (regs->gpr[rd]));
			goto logical_done;
#ifdef __powerpc64__
		case 58:	/* cntlzd */
			asm("cntlzd %0,%1" : "=r" (regs->gpr[ra]) :
			    "r" (regs->gpr[rd]));
			goto logical_done;
#endif
		case 28:	/* and */
			regs->gpr[ra] = regs->gpr[rd] & regs->gpr[rb];
			goto logical_done;

		case 60:	/* andc */
			regs->gpr[ra] = regs->gpr[rd] & ~regs->gpr[rb];
			goto logical_done;

		case 124:	/* nor */
			regs->gpr[ra] = ~(regs->gpr[rd] | regs->gpr[rb]);
			goto logical_done;

		case 284:	/* xor */
			regs->gpr[ra] = ~(regs->gpr[rd] ^ regs->gpr[rb]);
			goto logical_done;

		case 316:	/* xor */
			regs->gpr[ra] = regs->gpr[rd] ^ regs->gpr[rb];
			goto logical_done;

		case 412:	/* orc */
			regs->gpr[ra] = regs->gpr[rd] | ~regs->gpr[rb];
			goto logical_done;

		case 444:	/* or */
			regs->gpr[ra] = regs->gpr[rd] | regs->gpr[rb];
			goto logical_done;

		case 476:	/* nand */
			regs->gpr[ra] = ~(regs->gpr[rd] & regs->gpr[rb]);
			goto logical_done;

		case 922:	/* extsh */
			regs->gpr[ra] = (signed short) regs->gpr[rd];
			goto logical_done;

		case 954:	/* extsb */
			regs->gpr[ra] = (signed char) regs->gpr[rd];
			goto logical_done;
#ifdef __powerpc64__
		case 986:	/* extsw */
			regs->gpr[ra] = (signed int) regs->gpr[rd];
			goto logical_done;
#endif

/*
 * Shift instructions
 */
		case 24:	/* slw */
			sh = regs->gpr[rb] & 0x3f;
			if (sh < 32)
				regs->gpr[ra] = (regs->gpr[rd] << sh) & 0xffffffffUL;
			else
				regs->gpr[ra] = 0;
			goto logical_done;

		case 536:	/* srw */
			sh = regs->gpr[rb] & 0x3f;
			if (sh < 32)
				regs->gpr[ra] = (regs->gpr[rd] & 0xffffffffUL) >> sh;
			else
				regs->gpr[ra] = 0;
			goto logical_done;

		case 792:	/* sraw */
			sh = regs->gpr[rb] & 0x3f;
			ival = (signed int) regs->gpr[rd];
			regs->gpr[ra] = ival >> (sh < 32 ? sh : 31);
			if (ival < 0 && (sh >= 32 || (ival & ((1ul << sh) - 1)) != 0))
				regs->xer |= XER_CA;
			else
				regs->xer &= ~XER_CA;
			goto logical_done;

		case 824:	/* srawi */
			sh = rb;
			ival = (signed int) regs->gpr[rd];
			regs->gpr[ra] = ival >> sh;
			if (ival < 0 && (ival & ((1ul << sh) - 1)) != 0)
				regs->xer |= XER_CA;
			else
				regs->xer &= ~XER_CA;
			goto logical_done;

#ifdef __powerpc64__
		case 27:	/* sld */
			sh = regs->gpr[rb] & 0x7f;
			if (sh < 64)
				regs->gpr[ra] = regs->gpr[rd] << sh;
			else
				regs->gpr[ra] = 0;
			goto logical_done;

		case 539:	/* srd */
			sh = regs->gpr[rb] & 0x7f;
			if (sh < 64)
				regs->gpr[ra] = regs->gpr[rd] >> sh;
			else
				regs->gpr[ra] = 0;
			goto logical_done;

		case 794:	/* srad */
			sh = regs->gpr[rb] & 0x7f;
			ival = (signed long int) regs->gpr[rd];
			regs->gpr[ra] = ival >> (sh < 64 ? sh : 63);
			if (ival < 0 && (sh >= 64 || (ival & ((1ul << sh) - 1)) != 0))
				regs->xer |= XER_CA;
			else
				regs->xer &= ~XER_CA;
			goto logical_done;

		case 826:	/* sradi with sh_5 = 0 */
		case 827:	/* sradi with sh_5 = 1 */
			sh = rb | ((instr & 2) << 4);
			ival = (signed long int) regs->gpr[rd];
			regs->gpr[ra] = ival >> sh;
			if (ival < 0 && (ival & ((1ul << sh) - 1)) != 0)
				regs->xer |= XER_CA;
			else
				regs->xer &= ~XER_CA;
			goto logical_done;
#endif /* __powerpc64__ */

/*
 * Cache instructions
 */
		case 54:	/* dcbst */
			op->type = MKOP(CACHEOP, DCBST, 0);
			op->ea = xform_ea(instr, regs);
			return 0;

		case 86:	/* dcbf */
			op->type = MKOP(CACHEOP, DCBF, 0);
			op->ea = xform_ea(instr, regs);
			return 0;

		case 246:	/* dcbtst */
			op->type = MKOP(CACHEOP, DCBTST, 0);
			op->ea = xform_ea(instr, regs);
			op->reg = rd;
			return 0;

		case 278:	/* dcbt */
			op->type = MKOP(CACHEOP, DCBTST, 0);
			op->ea = xform_ea(instr, regs);
			op->reg = rd;
			return 0;

		case 982:	/* icbi */
			op->type = MKOP(CACHEOP, ICBI, 0);
			op->ea = xform_ea(instr, regs);
			return 0;
		}
		break;
	}

	/*
	 * Loads and stores.
	 */
	op->type = UNKNOWN;
	op->update_reg = ra;
	op->reg = rd;
	op->val = regs->gpr[rd];
	u = (instr >> 20) & UPDATE;

	switch (opcode) {
	case 31:
		u = instr & UPDATE;
		op->ea = xform_ea(instr, regs);
		switch ((instr >> 1) & 0x3ff) {
		case 20:	/* lwarx */
			op->type = MKOP(LARX, 0, 4);
			break;

		case 150:	/* stwcx. */
			op->type = MKOP(STCX, 0, 4);
			break;

#ifdef __powerpc64__
		case 84:	/* ldarx */
			op->type = MKOP(LARX, 0, 8);
			break;

		case 214:	/* stdcx. */
			op->type = MKOP(STCX, 0, 8);
			break;

		case 21:	/* ldx */
		case 53:	/* ldux */
			op->type = MKOP(LOAD, u, 8);
			break;
#endif

		case 23:	/* lwzx */
		case 55:	/* lwzux */
			op->type = MKOP(LOAD, u, 4);
			break;

		case 87:	/* lbzx */
		case 119:	/* lbzux */
			op->type = MKOP(LOAD, u, 1);
			break;

#ifdef CONFIG_ALTIVEC
		case 103:	/* lvx */
		case 359:	/* lvxl */
			if (!(regs->msr & MSR_VEC))
				goto vecunavail;
			op->type = MKOP(LOAD_VMX, 0, 16);
			break;

		case 231:	/* stvx */
		case 487:	/* stvxl */
			if (!(regs->msr & MSR_VEC))
				goto vecunavail;
			op->type = MKOP(STORE_VMX, 0, 16);
			break;
#endif /* CONFIG_ALTIVEC */

#ifdef __powerpc64__
		case 149:	/* stdx */
		case 181:	/* stdux */
			op->type = MKOP(STORE, u, 8);
			break;
#endif

		case 151:	/* stwx */
		case 183:	/* stwux */
			op->type = MKOP(STORE, u, 4);
			break;

		case 215:	/* stbx */
		case 247:	/* stbux */
			op->type = MKOP(STORE, u, 1);
			break;

		case 279:	/* lhzx */
		case 311:	/* lhzux */
			op->type = MKOP(LOAD, u, 2);
			break;

#ifdef __powerpc64__
		case 341:	/* lwax */
		case 373:	/* lwaux */
			op->type = MKOP(LOAD, SIGNEXT | u, 4);
			break;
#endif

		case 343:	/* lhax */
		case 375:	/* lhaux */
			op->type = MKOP(LOAD, SIGNEXT | u, 2);
			break;

		case 407:	/* sthx */
		case 439:	/* sthux */
			op->type = MKOP(STORE, u, 2);
			break;

#ifdef __powerpc64__
		case 532:	/* ldbrx */
			op->type = MKOP(LOAD, BYTEREV, 8);
			break;

#endif
		case 533:	/* lswx */
			op->type = MKOP(LOAD_MULTI, 0, regs->xer & 0x7f);
			break;

		case 534:	/* lwbrx */
			op->type = MKOP(LOAD, BYTEREV, 4);
			break;

		case 597:	/* lswi */
			if (rb == 0)
				rb = 32;	/* # bytes to load */
			op->type = MKOP(LOAD_MULTI, 0, rb);
			op->ea = 0;
			if (ra)
				op->ea = truncate_if_32bit(regs->msr,
							   regs->gpr[ra]);
			break;

#ifdef CONFIG_PPC_FPU
		case 535:	/* lfsx */
		case 567:	/* lfsux */
			if (!(regs->msr & MSR_FP))
				goto fpunavail;
			op->type = MKOP(LOAD_FP, u, 4);
			break;

		case 599:	/* lfdx */
		case 631:	/* lfdux */
			if (!(regs->msr & MSR_FP))
				goto fpunavail;
			op->type = MKOP(LOAD_FP, u, 8);
			break;

		case 663:	/* stfsx */
		case 695:	/* stfsux */
			if (!(regs->msr & MSR_FP))
				goto fpunavail;
			op->type = MKOP(STORE_FP, u, 4);
			break;

		case 727:	/* stfdx */
		case 759:	/* stfdux */
			if (!(regs->msr & MSR_FP))
				goto fpunavail;
			op->type = MKOP(STORE_FP, u, 8);
			break;
#endif

#ifdef __powerpc64__
		case 660:	/* stdbrx */
			op->type = MKOP(STORE, BYTEREV, 8);
			op->val = byterev_8(regs->gpr[rd]);
			break;

#endif
		case 661:	/* stswx */
			op->type = MKOP(STORE_MULTI, 0, regs->xer & 0x7f);
			break;

		case 662:	/* stwbrx */
			op->type = MKOP(STORE, BYTEREV, 4);
			op->val = byterev_4(regs->gpr[rd]);
			break;

		case 725:
			if (rb == 0)
				rb = 32;	/* # bytes to store */
			op->type = MKOP(STORE_MULTI, 0, rb);
			op->ea = 0;
			if (ra)
				op->ea = truncate_if_32bit(regs->msr,
							   regs->gpr[ra]);
			break;

		case 790:	/* lhbrx */
			op->type = MKOP(LOAD, BYTEREV, 2);
			break;

		case 918:	/* sthbrx */
			op->type = MKOP(STORE, BYTEREV, 2);
			op->val = byterev_2(regs->gpr[rd]);
			break;

#ifdef CONFIG_VSX
		case 844:	/* lxvd2x */
		case 876:	/* lxvd2ux */
			if (!(regs->msr & MSR_VSX))
				goto vsxunavail;
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, u, 16);
			break;

		case 972:	/* stxvd2x */
		case 1004:	/* stxvd2ux */
			if (!(regs->msr & MSR_VSX))
				goto vsxunavail;
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, u, 16);
			break;

#endif /* CONFIG_VSX */
		}
		break;

	case 32:	/* lwz */
	case 33:	/* lwzu */
		op->type = MKOP(LOAD, u, 4);
		op->ea = dform_ea(instr, regs);
		break;

	case 34:	/* lbz */
	case 35:	/* lbzu */
		op->type = MKOP(LOAD, u, 1);
		op->ea = dform_ea(instr, regs);
		break;

	case 36:	/* stw */
	case 37:	/* stwu */
		op->type = MKOP(STORE, u, 4);
		op->ea = dform_ea(instr, regs);
		break;

	case 38:	/* stb */
	case 39:	/* stbu */
		op->type = MKOP(STORE, u, 1);
		op->ea = dform_ea(instr, regs);
		break;

	case 40:	/* lhz */
	case 41:	/* lhzu */
		op->type = MKOP(LOAD, u, 2);
		op->ea = dform_ea(instr, regs);
		break;

	case 42:	/* lha */
	case 43:	/* lhau */
		op->type = MKOP(LOAD, SIGNEXT | u, 2);
		op->ea = dform_ea(instr, regs);
		break;

	case 44:	/* sth */
	case 45:	/* sthu */
		op->type = MKOP(STORE, u, 2);
		op->ea = dform_ea(instr, regs);
		break;

	case 46:	/* lmw */
		if (ra >= rd)
			break;		/* invalid form, ra in range to load */
		op->type = MKOP(LOAD_MULTI, 0, 4 * (32 - rd));
		op->ea = dform_ea(instr, regs);
		break;

	case 47:	/* stmw */
		op->type = MKOP(STORE_MULTI, 0, 4 * (32 - rd));
		op->ea = dform_ea(instr, regs);
		break;

#ifdef CONFIG_PPC_FPU
	case 48:	/* lfs */
	case 49:	/* lfsu */
		if (!(regs->msr & MSR_FP))
			goto fpunavail;
		op->type = MKOP(LOAD_FP, u, 4);
		op->ea = dform_ea(instr, regs);
		break;

	case 50:	/* lfd */
	case 51:	/* lfdu */
		if (!(regs->msr & MSR_FP))
			goto fpunavail;
		op->type = MKOP(LOAD_FP, u, 8);
		op->ea = dform_ea(instr, regs);
		break;

	case 52:	/* stfs */
	case 53:	/* stfsu */
		if (!(regs->msr & MSR_FP))
			goto fpunavail;
		op->type = MKOP(STORE_FP, u, 4);
		op->ea = dform_ea(instr, regs);
		break;

	case 54:	/* stfd */
	case 55:	/* stfdu */
		if (!(regs->msr & MSR_FP))
			goto fpunavail;
		op->type = MKOP(STORE_FP, u, 8);
		op->ea = dform_ea(instr, regs);
		break;
#endif

#ifdef __powerpc64__
	case 58:	/* ld[u], lwa */
		op->ea = dsform_ea(instr, regs);
		switch (instr & 3) {
		case 0:		/* ld */
			op->type = MKOP(LOAD, 0, 8);
			break;
		case 1:		/* ldu */
			op->type = MKOP(LOAD, UPDATE, 8);
			break;
		case 2:		/* lwa */
			op->type = MKOP(LOAD, SIGNEXT, 4);
			break;
		}
		break;

	case 62:	/* std[u] */
		op->ea = dsform_ea(instr, regs);
		switch (instr & 3) {
		case 0:		/* std */
			op->type = MKOP(STORE, 0, 8);
			break;
		case 1:		/* stdu */
			op->type = MKOP(STORE, UPDATE, 8);
			break;
		}
		break;
#endif /* __powerpc64__ */

	}
	return 0;

 logical_done:
	if (instr & 1)
		set_cr0(regs, ra);
	goto instr_done;

 arith_done:
	if (instr & 1)
		set_cr0(regs, rd);

 instr_done:
	regs->nip = truncate_if_32bit(regs->msr, regs->nip + 4);
	return 1;

 priv:
	op->type = INTERRUPT | 0x700;
	op->val = SRR1_PROGPRIV;
	return 0;

 trap:
	op->type = INTERRUPT | 0x700;
	op->val = SRR1_PROGTRAP;
	return 0;

#ifdef CONFIG_PPC_FPU
 fpunavail:
	op->type = INTERRUPT | 0x800;
	return 0;
#endif

#ifdef CONFIG_ALTIVEC
 vecunavail:
	op->type = INTERRUPT | 0xf20;
	return 0;
#endif

#ifdef CONFIG_VSX
 vsxunavail:
	op->type = INTERRUPT | 0xf40;
	return 0;
#endif
}
EXPORT_SYMBOL_GPL(analyse_instr);

/*
 * For PPC32 we always use stwu with r1 to change the stack pointer.
 * So this emulated store may corrupt the exception frame, now we
 * have to provide the exception frame trampoline, which is pushed
 * below the kprobed function stack. So we only update gpr[1] but
 * don't emulate the real store operation. We will do real store
 * operation safely in exception return code by checking this flag.
 */
static __kprobes int handle_stack_update(unsigned long ea, struct pt_regs *regs)
{
#ifdef CONFIG_PPC32
	/*
	 * Check if we will touch kernel stack overflow
	 */
	if (ea - STACK_INT_FRAME_SIZE <= current->thread.ksp_limit) {
		printk(KERN_CRIT "Can't kprobe this since kernel stack would overflow.\n");
		return -EINVAL;
	}
#endif /* CONFIG_PPC32 */
	/*
	 * Check if we already set since that means we'll
	 * lose the previous value.
	 */
	WARN_ON(test_thread_flag(TIF_EMULATE_STACK_STORE));
	set_thread_flag(TIF_EMULATE_STACK_STORE);
	return 0;
}

static __kprobes void do_signext(unsigned long *valp, int size)
{
	switch (size) {
	case 2:
		*valp = (signed short) *valp;
		break;
	case 4:
		*valp = (signed int) *valp;
		break;
	}
}

static __kprobes void do_byterev(unsigned long *valp, int size)
{
	switch (size) {
	case 2:
		*valp = byterev_2(*valp);
		break;
	case 4:
		*valp = byterev_4(*valp);
		break;
#ifdef __powerpc64__
	case 8:
		*valp = byterev_8(*valp);
		break;
#endif
	}
}

/*
 * Emulate instructions that cause a transfer of control,
 * loads and stores, and a few other instructions.
 * Returns 1 if the step was emulated, 0 if not,
 * or -1 if the instruction is one that should not be stepped,
 * such as an rfid, or a mtmsrd that would clear MSR_RI.
 */
int __kprobes emulate_step(struct pt_regs *regs, unsigned int instr)
{
	struct instruction_op op;
	int r, err, size;
	unsigned long val;
	unsigned int cr;
	int i, rd, nb;

	r = analyse_instr(&op, regs, instr);
	if (r != 0)
		return r;

	err = 0;
	size = GETSIZE(op.type);
	switch (op.type & INSTR_TYPE_MASK) {
	case CACHEOP:
		if (!address_ok(regs, op.ea, 8))
			return 0;
		switch (op.type & CACHEOP_MASK) {
		case DCBST:
			__cacheop_user_asmx(op.ea, err, "dcbst");
			break;
		case DCBF:
			__cacheop_user_asmx(op.ea, err, "dcbf");
			break;
		case DCBTST:
			if (op.reg == 0)
				prefetchw((void *) op.ea);
			break;
		case DCBT:
			if (op.reg == 0)
				prefetch((void *) op.ea);
			break;
		case ICBI:
			__cacheop_user_asmx(op.ea, err, "icbi");
			break;
		}
		if (err)
			return 0;
		goto instr_done;

	case LARX:
		if (op.ea & (size - 1))
			break;		/* can't handle misaligned */
		err = -EFAULT;
		if (!address_ok(regs, op.ea, size))
			goto ldst_done;
		err = 0;
		switch (size) {
		case 4:
			__get_user_asmx(val, op.ea, err, "lwarx");
			break;
		case 8:
			__get_user_asmx(val, op.ea, err, "ldarx");
			break;
		default:
			return 0;
		}
		if (!err)
			regs->gpr[op.reg] = val;
		goto ldst_done;

	case STCX:
		if (op.ea & (size - 1))
			break;		/* can't handle misaligned */
		err = -EFAULT;
		if (!address_ok(regs, op.ea, size))
			goto ldst_done;
		err = 0;
		switch (size) {
		case 4:
			__put_user_asmx(op.val, op.ea, err, "stwcx.", cr);
			break;
		case 8:
			__put_user_asmx(op.val, op.ea, err, "stdcx.", cr);
			break;
		default:
			return 0;
		}
		if (!err)
			regs->ccr = (regs->ccr & 0x0fffffff) |
				(cr & 0xe0000000) |
				((regs->xer >> 3) & 0x10000000);
		goto ldst_done;

	case LOAD:
		err = read_mem(&regs->gpr[op.reg], op.ea, size, regs);
		if (!err) {
			if (op.type & SIGNEXT)
				do_signext(&regs->gpr[op.reg], size);
			if (op.type & BYTEREV)
				do_byterev(&regs->gpr[op.reg], size);
		}
		goto ldst_done;

#ifdef CONFIG_PPC_FPU
	case LOAD_FP:
		if (size == 4)
			err = do_fp_load(op.reg, do_lfs, op.ea, size, regs);
		else
			err = do_fp_load(op.reg, do_lfd, op.ea, size, regs);
		goto ldst_done;
#endif
#ifdef CONFIG_ALTIVEC
	case LOAD_VMX:
		err = do_vec_load(op.reg, do_lvx, op.ea & ~0xfUL, regs);
		goto ldst_done;
#endif
#ifdef CONFIG_VSX
	case LOAD_VSX:
		err = do_vsx_load(op.reg, do_lxvd2x, op.ea, regs);
		goto ldst_done;
#endif
	case LOAD_MULTI:
		if (regs->msr & MSR_LE)
			return 0;
		rd = op.reg;
		for (i = 0; i < size; i += 4) {
			nb = size - i;
			if (nb > 4)
				nb = 4;
			err = read_mem(&regs->gpr[rd], op.ea, nb, regs);
			if (err)
				return 0;
			if (nb < 4)	/* left-justify last bytes */
				regs->gpr[rd] <<= 32 - 8 * nb;
			op.ea += 4;
			++rd;
		}
		goto instr_done;

	case STORE:
		if ((op.type & UPDATE) && size == sizeof(long) &&
		    op.reg == 1 && op.update_reg == 1 &&
		    !(regs->msr & MSR_PR) &&
		    op.ea >= regs->gpr[1] - STACK_INT_FRAME_SIZE) {
			err = handle_stack_update(op.ea, regs);
			goto ldst_done;
		}
		err = write_mem(op.val, op.ea, size, regs);
		goto ldst_done;

#ifdef CONFIG_PPC_FPU
	case STORE_FP:
		if (size == 4)
			err = do_fp_store(op.reg, do_stfs, op.ea, size, regs);
		else
			err = do_fp_store(op.reg, do_stfd, op.ea, size, regs);
		goto ldst_done;
#endif
#ifdef CONFIG_ALTIVEC
	case STORE_VMX:
		err = do_vec_store(op.reg, do_stvx, op.ea & ~0xfUL, regs);
		goto ldst_done;
#endif
#ifdef CONFIG_VSX
	case STORE_VSX:
		err = do_vsx_store(op.reg, do_stxvd2x, op.ea, regs);
		goto ldst_done;
#endif
	case STORE_MULTI:
		if (regs->msr & MSR_LE)
			return 0;
		rd = op.reg;
		for (i = 0; i < size; i += 4) {
			val = regs->gpr[rd];
			nb = size - i;
			if (nb > 4)
				nb = 4;
			else
				val >>= 32 - 8 * nb;
			err = write_mem(val, op.ea, nb, regs);
			if (err)
				return 0;
			op.ea += 4;
			++rd;
		}
		goto instr_done;

	case MFMSR:
		regs->gpr[op.reg] = regs->msr & MSR_MASK;
		goto instr_done;

	case MTMSR:
		val = regs->gpr[op.reg];
		if ((val & MSR_RI) == 0)
			/* can't step mtmsr[d] that would clear MSR_RI */
			return -1;
		/* here op.val is the mask of bits to change */
		regs->msr = (regs->msr & ~op.val) | (val & op.val);
		goto instr_done;

#ifdef CONFIG_PPC64
	case SYSCALL:	/* sc */
		/*
		 * N.B. this uses knowledge about how the syscall
		 * entry code works.  If that is changed, this will
		 * need to be changed also.
		 */
		if (regs->gpr[0] == 0x1ebe &&
		    cpu_has_feature(CPU_FTR_REAL_LE)) {
			regs->msr ^= MSR_LE;
			goto instr_done;
		}
		regs->gpr[9] = regs->gpr[13];
		regs->gpr[10] = MSR_KERNEL;
		regs->gpr[11] = regs->nip + 4;
		regs->gpr[12] = regs->msr & MSR_MASK;
		regs->gpr[13] = (unsigned long) get_paca();
		regs->nip = (unsigned long) &system_call_common;
		regs->msr = MSR_KERNEL;
		return 1;

	case RFI:
		return -1;
#endif
	}
	return 0;

 ldst_done:
	if (err)
		return 0;
	if (op.type & UPDATE)
		regs->gpr[op.update_reg] = op.ea;

 instr_done:
	regs->nip = truncate_if_32bit(regs->msr, regs->nip + 4);
	return 1;
}
