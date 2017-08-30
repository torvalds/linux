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
#include <linux/uaccess.h>
#include <asm/cpu_has_feature.h>
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
extern void load_vsrn(int vsr, const void *p);
extern void store_vsrn(int vsr, void *p);
extern void conv_sp_to_dp(const float *sp, double *dp);
extern void conv_dp_to_sp(const double *dp, float *sp);
#endif

#ifdef __powerpc64__
/*
 * Functions in quad.S
 */
extern int do_lq(unsigned long ea, unsigned long *regs);
extern int do_stq(unsigned long ea, unsigned long val0, unsigned long val1);
extern int do_lqarx(unsigned long ea, unsigned long *regs);
extern int do_stqcx(unsigned long ea, unsigned long val0, unsigned long val1,
		    unsigned int *crp);
#endif

#ifdef __LITTLE_ENDIAN__
#define IS_LE	1
#define IS_BE	0
#else
#define IS_LE	0
#define IS_BE	1
#endif

/*
 * Emulate the truncation of 64 bit values in 32-bit mode.
 */
static nokprobe_inline unsigned long truncate_if_32bit(unsigned long msr,
							unsigned long val)
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
static nokprobe_inline int branch_taken(unsigned int instr,
					const struct pt_regs *regs,
					struct instruction_op *op)
{
	unsigned int bo = (instr >> 21) & 0x1f;
	unsigned int bi;

	if ((bo & 4) == 0) {
		/* decrement counter */
		op->type |= DECCTR;
		if (((bo >> 1) & 1) ^ (regs->ctr == 1))
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

static nokprobe_inline long address_ok(struct pt_regs *regs, unsigned long ea, int nb)
{
	if (!user_mode(regs))
		return 1;
	return __access_ok(ea, nb, USER_DS);
}

/*
 * Calculate effective address for a D-form instruction
 */
static nokprobe_inline unsigned long dform_ea(unsigned int instr,
					      const struct pt_regs *regs)
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
static nokprobe_inline unsigned long dsform_ea(unsigned int instr,
					       const struct pt_regs *regs)
{
	int ra;
	unsigned long ea;

	ra = (instr >> 16) & 0x1f;
	ea = (signed short) (instr & ~3);	/* sign-extend */
	if (ra)
		ea += regs->gpr[ra];

	return truncate_if_32bit(regs->msr, ea);
}

/*
 * Calculate effective address for a DQ-form instruction
 */
static nokprobe_inline unsigned long dqform_ea(unsigned int instr,
					       const struct pt_regs *regs)
{
	int ra;
	unsigned long ea;

	ra = (instr >> 16) & 0x1f;
	ea = (signed short) (instr & ~0xf);	/* sign-extend */
	if (ra)
		ea += regs->gpr[ra];

	return truncate_if_32bit(regs->msr, ea);
}
#endif /* __powerpc64 */

/*
 * Calculate effective address for an X-form instruction
 */
static nokprobe_inline unsigned long xform_ea(unsigned int instr,
					      const struct pt_regs *regs)
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
static nokprobe_inline unsigned long max_align(unsigned long x)
{
	x |= sizeof(unsigned long);
	return x & -x;		/* isolates rightmost bit */
}


static nokprobe_inline unsigned long byterev_2(unsigned long x)
{
	return ((x >> 8) & 0xff) | ((x & 0xff) << 8);
}

static nokprobe_inline unsigned long byterev_4(unsigned long x)
{
	return ((x >> 24) & 0xff) | ((x >> 8) & 0xff00) |
		((x & 0xff00) << 8) | ((x & 0xff) << 24);
}

#ifdef __powerpc64__
static nokprobe_inline unsigned long byterev_8(unsigned long x)
{
	return (byterev_4(x) << 32) | byterev_4(x >> 32);
}
#endif

static nokprobe_inline int read_mem_aligned(unsigned long *dest,
					unsigned long ea, int nb)
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

static nokprobe_inline int read_mem_unaligned(unsigned long *dest,
				unsigned long ea, int nb, struct pt_regs *regs)
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
static int read_mem(unsigned long *dest, unsigned long ea, int nb,
			      struct pt_regs *regs)
{
	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	if ((ea & (nb - 1)) == 0)
		return read_mem_aligned(dest, ea, nb);
	return read_mem_unaligned(dest, ea, nb, regs);
}
NOKPROBE_SYMBOL(read_mem);

static nokprobe_inline int write_mem_aligned(unsigned long val,
					unsigned long ea, int nb)
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

static nokprobe_inline int write_mem_unaligned(unsigned long val,
				unsigned long ea, int nb, struct pt_regs *regs)
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
static int write_mem(unsigned long val, unsigned long ea, int nb,
			       struct pt_regs *regs)
{
	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	if ((ea & (nb - 1)) == 0)
		return write_mem_aligned(val, ea, nb);
	return write_mem_unaligned(val, ea, nb, regs);
}
NOKPROBE_SYMBOL(write_mem);

#ifdef CONFIG_PPC_FPU
/*
 * Check the address and alignment, and call func to do the actual
 * load or store.
 */
static int do_fp_load(int rn, int (*func)(int, unsigned long),
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
NOKPROBE_SYMBOL(do_fp_load);

static int do_fp_store(int rn, int (*func)(int, unsigned long),
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
NOKPROBE_SYMBOL(do_fp_store);
#endif

#ifdef CONFIG_ALTIVEC
/* For Altivec/VMX, no need to worry about alignment */
static nokprobe_inline int do_vec_load(int rn, int (*func)(int, unsigned long),
				 unsigned long ea, struct pt_regs *regs)
{
	if (!address_ok(regs, ea & ~0xfUL, 16))
		return -EFAULT;
	return (*func)(rn, ea);
}

static nokprobe_inline int do_vec_store(int rn, int (*func)(int, unsigned long),
				  unsigned long ea, struct pt_regs *regs)
{
	if (!address_ok(regs, ea & ~0xfUL, 16))
		return -EFAULT;
	return (*func)(rn, ea);
}
#endif /* CONFIG_ALTIVEC */

#ifdef __powerpc64__
static nokprobe_inline int emulate_lq(struct pt_regs *regs, unsigned long ea,
				      int reg)
{
	int err;

	if (!address_ok(regs, ea, 16))
		return -EFAULT;
	/* if aligned, should be atomic */
	if ((ea & 0xf) == 0)
		return do_lq(ea, &regs->gpr[reg]);

	err = read_mem(&regs->gpr[reg + IS_LE], ea, 8, regs);
	if (!err)
		err = read_mem(&regs->gpr[reg + IS_BE], ea + 8, 8, regs);
	return err;
}

static nokprobe_inline int emulate_stq(struct pt_regs *regs, unsigned long ea,
				       int reg)
{
	int err;

	if (!address_ok(regs, ea, 16))
		return -EFAULT;
	/* if aligned, should be atomic */
	if ((ea & 0xf) == 0)
		return do_stq(ea, regs->gpr[reg], regs->gpr[reg + 1]);

	err = write_mem(regs->gpr[reg + IS_LE], ea, 8, regs);
	if (!err)
		err = write_mem(regs->gpr[reg + IS_BE], ea + 8, 8, regs);
	return err;
}
#endif /* __powerpc64 */

#ifdef CONFIG_VSX
void emulate_vsx_load(struct instruction_op *op, union vsx_reg *reg,
		      const void *mem)
{
	int size, read_size;
	int i, j;
	const unsigned int *wp;
	const unsigned short *hp;
	const unsigned char *bp;

	size = GETSIZE(op->type);
	reg->d[0] = reg->d[1] = 0;

	switch (op->element_size) {
	case 16:
		/* whole vector; lxv[x] or lxvl[l] */
		if (size == 0)
			break;
		memcpy(reg, mem, size);
		if (IS_LE && (op->vsx_flags & VSX_LDLEFT)) {
			/* reverse 16 bytes */
			unsigned long tmp;
			tmp = byterev_8(reg->d[0]);
			reg->d[0] = byterev_8(reg->d[1]);
			reg->d[1] = tmp;
		}
		break;
	case 8:
		/* scalar loads, lxvd2x, lxvdsx */
		read_size = (size >= 8) ? 8 : size;
		i = IS_LE ? 8 : 8 - read_size;
		memcpy(&reg->b[i], mem, read_size);
		if (size < 8) {
			if (op->type & SIGNEXT) {
				/* size == 4 is the only case here */
				reg->d[IS_LE] = (signed int) reg->d[IS_LE];
			} else if (op->vsx_flags & VSX_FPCONV) {
				preempt_disable();
				conv_sp_to_dp(&reg->fp[1 + IS_LE],
					      &reg->dp[IS_LE]);
				preempt_enable();
			}
		} else {
			if (size == 16)
				reg->d[IS_BE] = *(unsigned long *)(mem + 8);
			else if (op->vsx_flags & VSX_SPLAT)
				reg->d[IS_BE] = reg->d[IS_LE];
		}
		break;
	case 4:
		/* lxvw4x, lxvwsx */
		wp = mem;
		for (j = 0; j < size / 4; ++j) {
			i = IS_LE ? 3 - j : j;
			reg->w[i] = *wp++;
		}
		if (op->vsx_flags & VSX_SPLAT) {
			u32 val = reg->w[IS_LE ? 3 : 0];
			for (; j < 4; ++j) {
				i = IS_LE ? 3 - j : j;
				reg->w[i] = val;
			}
		}
		break;
	case 2:
		/* lxvh8x */
		hp = mem;
		for (j = 0; j < size / 2; ++j) {
			i = IS_LE ? 7 - j : j;
			reg->h[i] = *hp++;
		}
		break;
	case 1:
		/* lxvb16x */
		bp = mem;
		for (j = 0; j < size; ++j) {
			i = IS_LE ? 15 - j : j;
			reg->b[i] = *bp++;
		}
		break;
	}
}
EXPORT_SYMBOL_GPL(emulate_vsx_load);
NOKPROBE_SYMBOL(emulate_vsx_load);

void emulate_vsx_store(struct instruction_op *op, const union vsx_reg *reg,
		       void *mem)
{
	int size, write_size;
	int i, j;
	union vsx_reg buf;
	unsigned int *wp;
	unsigned short *hp;
	unsigned char *bp;

	size = GETSIZE(op->type);

	switch (op->element_size) {
	case 16:
		/* stxv, stxvx, stxvl, stxvll */
		if (size == 0)
			break;
		if (IS_LE && (op->vsx_flags & VSX_LDLEFT)) {
			/* reverse 16 bytes */
			buf.d[0] = byterev_8(reg->d[1]);
			buf.d[1] = byterev_8(reg->d[0]);
			reg = &buf;
		}
		memcpy(mem, reg, size);
		break;
	case 8:
		/* scalar stores, stxvd2x */
		write_size = (size >= 8) ? 8 : size;
		i = IS_LE ? 8 : 8 - write_size;
		if (size < 8 && op->vsx_flags & VSX_FPCONV) {
			buf.d[0] = buf.d[1] = 0;
			preempt_disable();
			conv_dp_to_sp(&reg->dp[IS_LE], &buf.fp[1 + IS_LE]);
			preempt_enable();
			reg = &buf;
		}
		memcpy(mem, &reg->b[i], write_size);
		if (size == 16)
			memcpy(mem + 8, &reg->d[IS_BE], 8);
		break;
	case 4:
		/* stxvw4x */
		wp = mem;
		for (j = 0; j < size / 4; ++j) {
			i = IS_LE ? 3 - j : j;
			*wp++ = reg->w[i];
		}
		break;
	case 2:
		/* stxvh8x */
		hp = mem;
		for (j = 0; j < size / 2; ++j) {
			i = IS_LE ? 7 - j : j;
			*hp++ = reg->h[i];
		}
		break;
	case 1:
		/* stvxb16x */
		bp = mem;
		for (j = 0; j < size; ++j) {
			i = IS_LE ? 15 - j : j;
			*bp++ = reg->b[i];
		}
		break;
	}
}
EXPORT_SYMBOL_GPL(emulate_vsx_store);
NOKPROBE_SYMBOL(emulate_vsx_store);
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
		EX_TABLE(1b, 3b)			\
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
		EX_TABLE(1b, 3b)			\
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
		EX_TABLE(1b, 3b)			\
		: "=r" (err)				\
		: "r" (addr), "i" (-EFAULT), "0" (err))

static nokprobe_inline void set_cr0(const struct pt_regs *regs,
				    struct instruction_op *op, int rd)
{
	long val = regs->gpr[rd];

	op->type |= SETCC;
	op->ccval = (regs->ccr & 0x0fffffff) | ((regs->xer >> 3) & 0x10000000);
#ifdef __powerpc64__
	if (!(regs->msr & MSR_64BIT))
		val = (int) val;
#endif
	if (val < 0)
		op->ccval |= 0x80000000;
	else if (val > 0)
		op->ccval |= 0x40000000;
	else
		op->ccval |= 0x20000000;
}

static nokprobe_inline void add_with_carry(const struct pt_regs *regs,
				     struct instruction_op *op, int rd,
				     unsigned long val1, unsigned long val2,
				     unsigned long carry_in)
{
	unsigned long val = val1 + val2;

	if (carry_in)
		++val;
	op->type = COMPUTE + SETREG + SETXER;
	op->reg = rd;
	op->val = val;
#ifdef __powerpc64__
	if (!(regs->msr & MSR_64BIT)) {
		val = (unsigned int) val;
		val1 = (unsigned int) val1;
	}
#endif
	op->xerval = regs->xer;
	if (val < val1 || (carry_in && val == val1))
		op->xerval |= XER_CA;
	else
		op->xerval &= ~XER_CA;
}

static nokprobe_inline void do_cmp_signed(const struct pt_regs *regs,
					  struct instruction_op *op,
					  long v1, long v2, int crfld)
{
	unsigned int crval, shift;

	op->type = COMPUTE + SETCC;
	crval = (regs->xer >> 31) & 1;		/* get SO bit */
	if (v1 < v2)
		crval |= 8;
	else if (v1 > v2)
		crval |= 4;
	else
		crval |= 2;
	shift = (7 - crfld) * 4;
	op->ccval = (regs->ccr & ~(0xf << shift)) | (crval << shift);
}

static nokprobe_inline void do_cmp_unsigned(const struct pt_regs *regs,
					    struct instruction_op *op,
					    unsigned long v1,
					    unsigned long v2, int crfld)
{
	unsigned int crval, shift;

	op->type = COMPUTE + SETCC;
	crval = (regs->xer >> 31) & 1;		/* get SO bit */
	if (v1 < v2)
		crval |= 8;
	else if (v1 > v2)
		crval |= 4;
	else
		crval |= 2;
	shift = (7 - crfld) * 4;
	op->ccval = (regs->ccr & ~(0xf << shift)) | (crval << shift);
}

static nokprobe_inline void do_cmpb(const struct pt_regs *regs,
				    struct instruction_op *op,
				    unsigned long v1, unsigned long v2)
{
	unsigned long long out_val, mask;
	int i;

	out_val = 0;
	for (i = 0; i < 8; i++) {
		mask = 0xffUL << (i * 8);
		if ((v1 & mask) == (v2 & mask))
			out_val |= mask;
	}
	op->val = out_val;
}

/*
 * The size parameter is used to adjust the equivalent popcnt instruction.
 * popcntb = 8, popcntw = 32, popcntd = 64
 */
static nokprobe_inline void do_popcnt(const struct pt_regs *regs,
				      struct instruction_op *op,
				      unsigned long v1, int size)
{
	unsigned long long out = v1;

	out -= (out >> 1) & 0x5555555555555555;
	out = (0x3333333333333333 & out) + (0x3333333333333333 & (out >> 2));
	out = (out + (out >> 4)) & 0x0f0f0f0f0f0f0f0f;

	if (size == 8) {	/* popcntb */
		op->val = out;
		return;
	}
	out += out >> 8;
	out += out >> 16;
	if (size == 32) {	/* popcntw */
		op->val = out & 0x0000003f0000003f;
		return;
	}

	out = (out + (out >> 32)) & 0x7f;
	op->val = out;	/* popcntd */
}

#ifdef CONFIG_PPC64
static nokprobe_inline void do_bpermd(const struct pt_regs *regs,
				      struct instruction_op *op,
				      unsigned long v1, unsigned long v2)
{
	unsigned char perm, idx;
	unsigned int i;

	perm = 0;
	for (i = 0; i < 8; i++) {
		idx = (v1 >> (i * 8)) & 0xff;
		if (idx < 64)
			if (v2 & PPC_BIT(idx))
				perm |= 1 << i;
	}
	op->val = perm;
}
#endif /* CONFIG_PPC64 */
/*
 * The size parameter adjusts the equivalent prty instruction.
 * prtyw = 32, prtyd = 64
 */
static nokprobe_inline void do_prty(const struct pt_regs *regs,
				    struct instruction_op *op,
				    unsigned long v, int size)
{
	unsigned long long res = v ^ (v >> 8);

	res ^= res >> 16;
	if (size == 32) {		/* prtyw */
		op->val = res & 0x0000000100000001;
		return;
	}

	res ^= res >> 32;
	op->val = res & 1;	/*prtyd */
}

static nokprobe_inline int trap_compare(long v1, long v2)
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
 * Decode an instruction, and return information about it in *op
 * without changing *regs.
 * Integer arithmetic and logical instructions, branches, and barrier
 * instructions can be emulated just using the information in *op.
 *
 * Return value is 1 if the instruction can be emulated just by
 * updating *regs with the information in *op, -1 if we need the
 * GPRs but *regs doesn't contain the full register set, or 0
 * otherwise.
 */
int analyse_instr(struct instruction_op *op, const struct pt_regs *regs,
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
		op->val = truncate_if_32bit(regs->msr, imm);
		if (instr & 1)
			op->type |= SETLK;
		if (branch_taken(instr, regs, op))
			op->type |= BRTAKEN;
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
		op->type = BRANCH | BRTAKEN;
		imm = instr & 0x03fffffc;
		if (imm & 0x02000000)
			imm -= 0x04000000;
		if ((instr & 2) == 0)
			imm += regs->nip;
		op->val = truncate_if_32bit(regs->msr, imm);
		if (instr & 1)
			op->type |= SETLK;
		return 1;
	case 19:
		switch ((instr >> 1) & 0x3ff) {
		case 0:		/* mcrf */
			op->type = COMPUTE + SETCC;
			rd = 7 - ((instr >> 23) & 0x7);
			ra = 7 - ((instr >> 18) & 0x7);
			rd *= 4;
			ra *= 4;
			val = (regs->ccr >> ra) & 0xf;
			op->ccval = (regs->ccr & ~(0xfUL << rd)) | (val << rd);
			return 1;

		case 16:	/* bclr */
		case 528:	/* bcctr */
			op->type = BRANCH;
			imm = (instr & 0x400)? regs->ctr: regs->link;
			op->val = truncate_if_32bit(regs->msr, imm);
			if (instr & 1)
				op->type |= SETLK;
			if (branch_taken(instr, regs, op))
				op->type |= BRTAKEN;
			return 1;

		case 18:	/* rfid, scary */
			if (regs->msr & MSR_PR)
				goto priv;
			op->type = RFI;
			return 0;

		case 150:	/* isync */
			op->type = BARRIER | BARRIER_ISYNC;
			return 1;

		case 33:	/* crnor */
		case 129:	/* crandc */
		case 193:	/* crxor */
		case 225:	/* crnand */
		case 257:	/* crand */
		case 289:	/* creqv */
		case 417:	/* crorc */
		case 449:	/* cror */
			op->type = COMPUTE + SETCC;
			ra = (instr >> 16) & 0x1f;
			rb = (instr >> 11) & 0x1f;
			rd = (instr >> 21) & 0x1f;
			ra = (regs->ccr >> (31 - ra)) & 1;
			rb = (regs->ccr >> (31 - rb)) & 1;
			val = (instr >> (6 + ra * 2 + rb)) & 1;
			op->ccval = (regs->ccr & ~(1UL << (31 - rd))) |
				(val << (31 - rd));
			return 1;
		default:
			op->type = UNKNOWN;
			return 0;
		}
		break;
	case 31:
		switch ((instr >> 1) & 0x3ff) {
		case 598:	/* sync */
			op->type = BARRIER + BARRIER_SYNC;
#ifdef __powerpc64__
			switch ((instr >> 21) & 3) {
			case 1:		/* lwsync */
				op->type = BARRIER + BARRIER_LWSYNC;
				break;
			case 2:		/* ptesync */
				op->type = BARRIER + BARRIER_PTESYNC;
				break;
			}
#endif
			return 1;

		case 854:	/* eieio */
			op->type = BARRIER + BARRIER_EIEIO;
			return 1;
		}
		break;
	}

	/* Following cases refer to regs->gpr[], so we need all regs */
	if (!FULL_REGS(regs))
		return -1;

	rd = (instr >> 21) & 0x1f;
	ra = (instr >> 16) & 0x1f;
	rb = (instr >> 11) & 0x1f;

	switch (opcode) {
#ifdef __powerpc64__
	case 2:		/* tdi */
		if (rd & trap_compare(regs->gpr[ra], (short) instr))
			goto trap;
		return 1;
#endif
	case 3:		/* twi */
		if (rd & trap_compare((int)regs->gpr[ra], (short) instr))
			goto trap;
		return 1;

	case 7:		/* mulli */
		op->val = regs->gpr[ra] * (short) instr;
		goto compute_done;

	case 8:		/* subfic */
		imm = (short) instr;
		add_with_carry(regs, op, rd, ~regs->gpr[ra], imm, 1);
		return 1;

	case 10:	/* cmpli */
		imm = (unsigned short) instr;
		val = regs->gpr[ra];
#ifdef __powerpc64__
		if ((rd & 1) == 0)
			val = (unsigned int) val;
#endif
		do_cmp_unsigned(regs, op, val, imm, rd >> 2);
		return 1;

	case 11:	/* cmpi */
		imm = (short) instr;
		val = regs->gpr[ra];
#ifdef __powerpc64__
		if ((rd & 1) == 0)
			val = (int) val;
#endif
		do_cmp_signed(regs, op, val, imm, rd >> 2);
		return 1;

	case 12:	/* addic */
		imm = (short) instr;
		add_with_carry(regs, op, rd, regs->gpr[ra], imm, 0);
		return 1;

	case 13:	/* addic. */
		imm = (short) instr;
		add_with_carry(regs, op, rd, regs->gpr[ra], imm, 0);
		set_cr0(regs, op, rd);
		return 1;

	case 14:	/* addi */
		imm = (short) instr;
		if (ra)
			imm += regs->gpr[ra];
		op->val = imm;
		goto compute_done;

	case 15:	/* addis */
		imm = ((short) instr) << 16;
		if (ra)
			imm += regs->gpr[ra];
		op->val = imm;
		goto compute_done;

	case 20:	/* rlwimi */
		mb = (instr >> 6) & 0x1f;
		me = (instr >> 1) & 0x1f;
		val = DATA32(regs->gpr[rd]);
		imm = MASK32(mb, me);
		op->val = (regs->gpr[ra] & ~imm) | (ROTATE(val, rb) & imm);
		goto logical_done;

	case 21:	/* rlwinm */
		mb = (instr >> 6) & 0x1f;
		me = (instr >> 1) & 0x1f;
		val = DATA32(regs->gpr[rd]);
		op->val = ROTATE(val, rb) & MASK32(mb, me);
		goto logical_done;

	case 23:	/* rlwnm */
		mb = (instr >> 6) & 0x1f;
		me = (instr >> 1) & 0x1f;
		rb = regs->gpr[rb] & 0x1f;
		val = DATA32(regs->gpr[rd]);
		op->val = ROTATE(val, rb) & MASK32(mb, me);
		goto logical_done;

	case 24:	/* ori */
		op->val = regs->gpr[rd] | (unsigned short) instr;
		goto logical_done_nocc;

	case 25:	/* oris */
		imm = (unsigned short) instr;
		op->val = regs->gpr[rd] | (imm << 16);
		goto logical_done_nocc;

	case 26:	/* xori */
		op->val = regs->gpr[rd] ^ (unsigned short) instr;
		goto logical_done_nocc;

	case 27:	/* xoris */
		imm = (unsigned short) instr;
		op->val = regs->gpr[rd] ^ (imm << 16);
		goto logical_done_nocc;

	case 28:	/* andi. */
		op->val = regs->gpr[rd] & (unsigned short) instr;
		set_cr0(regs, op, ra);
		goto logical_done_nocc;

	case 29:	/* andis. */
		imm = (unsigned short) instr;
		op->val = regs->gpr[rd] & (imm << 16);
		set_cr0(regs, op, ra);
		goto logical_done_nocc;

#ifdef __powerpc64__
	case 30:	/* rld* */
		mb = ((instr >> 6) & 0x1f) | (instr & 0x20);
		val = regs->gpr[rd];
		if ((instr & 0x10) == 0) {
			sh = rb | ((instr & 2) << 4);
			val = ROTATE(val, sh);
			switch ((instr >> 2) & 3) {
			case 0:		/* rldicl */
				val &= MASK64_L(mb);
				break;
			case 1:		/* rldicr */
				val &= MASK64_R(mb);
				break;
			case 2:		/* rldic */
				val &= MASK64(mb, 63 - sh);
				break;
			case 3:		/* rldimi */
				imm = MASK64(mb, 63 - sh);
				val = (regs->gpr[ra] & ~imm) |
					(val & imm);
			}
			op->val = val;
			goto logical_done;
		} else {
			sh = regs->gpr[rb] & 0x3f;
			val = ROTATE(val, sh);
			switch ((instr >> 1) & 7) {
			case 0:		/* rldcl */
				op->val = val & MASK64_L(mb);
				goto logical_done;
			case 1:		/* rldcr */
				op->val = val & MASK64_R(mb);
				goto logical_done;
			}
		}
#endif
		op->type = UNKNOWN;	/* illegal instruction */
		return 0;

	case 31:
		switch ((instr >> 1) & 0x3ff) {
		case 4:		/* tw */
			if (rd == 0x1f ||
			    (rd & trap_compare((int)regs->gpr[ra],
					       (int)regs->gpr[rb])))
				goto trap;
			return 1;
#ifdef __powerpc64__
		case 68:	/* td */
			if (rd & trap_compare(regs->gpr[ra], regs->gpr[rb]))
				goto trap;
			return 1;
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
			imm = 0xffffffffUL;
			if ((instr >> 20) & 1) {
				imm = 0xf0000000UL;
				for (sh = 0; sh < 8; ++sh) {
					if (instr & (0x80000 >> sh))
						break;
					imm >>= 4;
				}
			}
			op->val = regs->ccr & imm;
			goto compute_done;

		case 144:	/* mtcrf */
			op->type = COMPUTE + SETCC;
			imm = 0xf0000000UL;
			val = regs->gpr[rd];
			op->val = regs->ccr;
			for (sh = 0; sh < 8; ++sh) {
				if (instr & (0x80000 >> sh))
					op->val = (op->val & ~imm) |
						(val & imm);
				imm >>= 4;
			}
			return 1;

		case 339:	/* mfspr */
			spr = ((instr >> 16) & 0x1f) | ((instr >> 6) & 0x3e0);
			op->type = MFSPR;
			op->reg = rd;
			op->spr = spr;
			if (spr == SPRN_XER || spr == SPRN_LR ||
			    spr == SPRN_CTR)
				return 1;
			return 0;

		case 467:	/* mtspr */
			spr = ((instr >> 16) & 0x1f) | ((instr >> 6) & 0x3e0);
			op->type = MTSPR;
			op->val = regs->gpr[rd];
			op->spr = spr;
			if (spr == SPRN_XER || spr == SPRN_LR ||
			    spr == SPRN_CTR)
				return 1;
			return 0;

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
			do_cmp_signed(regs, op, val, val2, rd >> 2);
			return 1;

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
			do_cmp_unsigned(regs, op, val, val2, rd >> 2);
			return 1;

		case 508: /* cmpb */
			do_cmpb(regs, op, regs->gpr[rd], regs->gpr[rb]);
			goto logical_done_nocc;

/*
 * Arithmetic instructions
 */
		case 8:	/* subfc */
			add_with_carry(regs, op, rd, ~regs->gpr[ra],
				       regs->gpr[rb], 1);
			goto arith_done;
#ifdef __powerpc64__
		case 9:	/* mulhdu */
			asm("mulhdu %0,%1,%2" : "=r" (op->val) :
			    "r" (regs->gpr[ra]), "r" (regs->gpr[rb]));
			goto arith_done;
#endif
		case 10:	/* addc */
			add_with_carry(regs, op, rd, regs->gpr[ra],
				       regs->gpr[rb], 0);
			goto arith_done;

		case 11:	/* mulhwu */
			asm("mulhwu %0,%1,%2" : "=r" (op->val) :
			    "r" (regs->gpr[ra]), "r" (regs->gpr[rb]));
			goto arith_done;

		case 40:	/* subf */
			op->val = regs->gpr[rb] - regs->gpr[ra];
			goto arith_done;
#ifdef __powerpc64__
		case 73:	/* mulhd */
			asm("mulhd %0,%1,%2" : "=r" (op->val) :
			    "r" (regs->gpr[ra]), "r" (regs->gpr[rb]));
			goto arith_done;
#endif
		case 75:	/* mulhw */
			asm("mulhw %0,%1,%2" : "=r" (op->val) :
			    "r" (regs->gpr[ra]), "r" (regs->gpr[rb]));
			goto arith_done;

		case 104:	/* neg */
			op->val = -regs->gpr[ra];
			goto arith_done;

		case 136:	/* subfe */
			add_with_carry(regs, op, rd, ~regs->gpr[ra],
				       regs->gpr[rb], regs->xer & XER_CA);
			goto arith_done;

		case 138:	/* adde */
			add_with_carry(regs, op, rd, regs->gpr[ra],
				       regs->gpr[rb], regs->xer & XER_CA);
			goto arith_done;

		case 200:	/* subfze */
			add_with_carry(regs, op, rd, ~regs->gpr[ra], 0L,
				       regs->xer & XER_CA);
			goto arith_done;

		case 202:	/* addze */
			add_with_carry(regs, op, rd, regs->gpr[ra], 0L,
				       regs->xer & XER_CA);
			goto arith_done;

		case 232:	/* subfme */
			add_with_carry(regs, op, rd, ~regs->gpr[ra], -1L,
				       regs->xer & XER_CA);
			goto arith_done;
#ifdef __powerpc64__
		case 233:	/* mulld */
			op->val = regs->gpr[ra] * regs->gpr[rb];
			goto arith_done;
#endif
		case 234:	/* addme */
			add_with_carry(regs, op, rd, regs->gpr[ra], -1L,
				       regs->xer & XER_CA);
			goto arith_done;

		case 235:	/* mullw */
			op->val = (unsigned int) regs->gpr[ra] *
				(unsigned int) regs->gpr[rb];
			goto arith_done;

		case 266:	/* add */
			op->val = regs->gpr[ra] + regs->gpr[rb];
			goto arith_done;
#ifdef __powerpc64__
		case 457:	/* divdu */
			op->val = regs->gpr[ra] / regs->gpr[rb];
			goto arith_done;
#endif
		case 459:	/* divwu */
			op->val = (unsigned int) regs->gpr[ra] /
				(unsigned int) regs->gpr[rb];
			goto arith_done;
#ifdef __powerpc64__
		case 489:	/* divd */
			op->val = (long int) regs->gpr[ra] /
				(long int) regs->gpr[rb];
			goto arith_done;
#endif
		case 491:	/* divw */
			op->val = (int) regs->gpr[ra] /
				(int) regs->gpr[rb];
			goto arith_done;


/*
 * Logical instructions
 */
		case 15:	/* isel */
			mb = (instr >> 6) & 0x1f; /* bc */
			val = (regs->ccr >> (31 - mb)) & 1;
			val2 = (ra) ? regs->gpr[ra] : 0;

			op->val = (val) ? val2 : regs->gpr[rb];
			goto compute_done;

		case 26:	/* cntlzw */
			op->val = __builtin_clz((unsigned int) regs->gpr[rd]);
			goto logical_done;
#ifdef __powerpc64__
		case 58:	/* cntlzd */
			op->val = __builtin_clzl(regs->gpr[rd]);
			goto logical_done;
#endif
		case 28:	/* and */
			op->val = regs->gpr[rd] & regs->gpr[rb];
			goto logical_done;

		case 60:	/* andc */
			op->val = regs->gpr[rd] & ~regs->gpr[rb];
			goto logical_done;

		case 122:	/* popcntb */
			do_popcnt(regs, op, regs->gpr[rd], 8);
			goto logical_done;

		case 124:	/* nor */
			op->val = ~(regs->gpr[rd] | regs->gpr[rb]);
			goto logical_done;

		case 154:	/* prtyw */
			do_prty(regs, op, regs->gpr[rd], 32);
			goto logical_done;

		case 186:	/* prtyd */
			do_prty(regs, op, regs->gpr[rd], 64);
			goto logical_done;
#ifdef CONFIG_PPC64
		case 252:	/* bpermd */
			do_bpermd(regs, op, regs->gpr[rd], regs->gpr[rb]);
			goto logical_done;
#endif
		case 284:	/* xor */
			op->val = ~(regs->gpr[rd] ^ regs->gpr[rb]);
			goto logical_done;

		case 316:	/* xor */
			op->val = regs->gpr[rd] ^ regs->gpr[rb];
			goto logical_done;

		case 378:	/* popcntw */
			do_popcnt(regs, op, regs->gpr[rd], 32);
			goto logical_done;

		case 412:	/* orc */
			op->val = regs->gpr[rd] | ~regs->gpr[rb];
			goto logical_done;

		case 444:	/* or */
			op->val = regs->gpr[rd] | regs->gpr[rb];
			goto logical_done;

		case 476:	/* nand */
			op->val = ~(regs->gpr[rd] & regs->gpr[rb]);
			goto logical_done;
#ifdef CONFIG_PPC64
		case 506:	/* popcntd */
			do_popcnt(regs, op, regs->gpr[rd], 64);
			goto logical_done;
#endif
		case 922:	/* extsh */
			op->val = (signed short) regs->gpr[rd];
			goto logical_done;

		case 954:	/* extsb */
			op->val = (signed char) regs->gpr[rd];
			goto logical_done;
#ifdef __powerpc64__
		case 986:	/* extsw */
			op->val = (signed int) regs->gpr[rd];
			goto logical_done;
#endif

/*
 * Shift instructions
 */
		case 24:	/* slw */
			sh = regs->gpr[rb] & 0x3f;
			if (sh < 32)
				op->val = (regs->gpr[rd] << sh) & 0xffffffffUL;
			else
				op->val = 0;
			goto logical_done;

		case 536:	/* srw */
			sh = regs->gpr[rb] & 0x3f;
			if (sh < 32)
				op->val = (regs->gpr[rd] & 0xffffffffUL) >> sh;
			else
				op->val = 0;
			goto logical_done;

		case 792:	/* sraw */
			op->type = COMPUTE + SETREG + SETXER;
			sh = regs->gpr[rb] & 0x3f;
			ival = (signed int) regs->gpr[rd];
			op->val = ival >> (sh < 32 ? sh : 31);
			op->xerval = regs->xer;
			if (ival < 0 && (sh >= 32 || (ival & ((1ul << sh) - 1)) != 0))
				op->xerval |= XER_CA;
			else
				op->xerval &= ~XER_CA;
			goto logical_done;

		case 824:	/* srawi */
			op->type = COMPUTE + SETREG + SETXER;
			sh = rb;
			ival = (signed int) regs->gpr[rd];
			op->val = ival >> sh;
			op->xerval = regs->xer;
			if (ival < 0 && (ival & ((1ul << sh) - 1)) != 0)
				op->xerval |= XER_CA;
			else
				op->xerval &= ~XER_CA;
			goto logical_done;

#ifdef __powerpc64__
		case 27:	/* sld */
			sh = regs->gpr[rb] & 0x7f;
			if (sh < 64)
				op->val = regs->gpr[rd] << sh;
			else
				op->val = 0;
			goto logical_done;

		case 539:	/* srd */
			sh = regs->gpr[rb] & 0x7f;
			if (sh < 64)
				op->val = regs->gpr[rd] >> sh;
			else
				op->val = 0;
			goto logical_done;

		case 794:	/* srad */
			op->type = COMPUTE + SETREG + SETXER;
			sh = regs->gpr[rb] & 0x7f;
			ival = (signed long int) regs->gpr[rd];
			op->val = ival >> (sh < 64 ? sh : 63);
			op->xerval = regs->xer;
			if (ival < 0 && (sh >= 64 || (ival & ((1ul << sh) - 1)) != 0))
				op->xerval |= XER_CA;
			else
				op->xerval &= ~XER_CA;
			goto logical_done;

		case 826:	/* sradi with sh_5 = 0 */
		case 827:	/* sradi with sh_5 = 1 */
			op->type = COMPUTE + SETREG + SETXER;
			sh = rb | ((instr & 2) << 4);
			ival = (signed long int) regs->gpr[rd];
			op->val = ival >> sh;
			op->xerval = regs->xer;
			if (ival < 0 && (ival & ((1ul << sh) - 1)) != 0)
				op->xerval |= XER_CA;
			else
				op->xerval &= ~XER_CA;
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
	op->vsx_flags = 0;

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

		case 52:	/* lbarx */
			op->type = MKOP(LARX, 0, 1);
			break;

		case 694:	/* stbcx. */
			op->type = MKOP(STCX, 0, 1);
			break;

		case 116:	/* lharx */
			op->type = MKOP(LARX, 0, 2);
			break;

		case 726:	/* sthcx. */
			op->type = MKOP(STCX, 0, 2);
			break;

		case 276:	/* lqarx */
			if (!((rd & 1) || rd == ra || rd == rb))
				op->type = MKOP(LARX, 0, 16);
			break;

		case 182:	/* stqcx. */
			if (!(rd & 1))
				op->type = MKOP(STCX, 0, 16);
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
			op->type = MKOP(LOAD_VMX, 0, 16);
			op->element_size = 16;
			break;

		case 231:	/* stvx */
		case 487:	/* stvxl */
			op->type = MKOP(STORE_VMX, 0, 16);
			break;
#endif /* CONFIG_ALTIVEC */

#ifdef __powerpc64__
		case 21:	/* ldx */
		case 53:	/* ldux */
			op->type = MKOP(LOAD, u, 8);
			break;

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
			op->type = MKOP(LOAD_FP, u, 4);
			break;

		case 599:	/* lfdx */
		case 631:	/* lfdux */
			op->type = MKOP(LOAD_FP, u, 8);
			break;

		case 663:	/* stfsx */
		case 695:	/* stfsux */
			op->type = MKOP(STORE_FP, u, 4);
			break;

		case 727:	/* stfdx */
		case 759:	/* stfdux */
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
		case 12:	/* lxsiwzx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 4);
			op->element_size = 8;
			break;

		case 76:	/* lxsiwax */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, SIGNEXT, 4);
			op->element_size = 8;
			break;

		case 140:	/* stxsiwx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 4);
			op->element_size = 8;
			break;

		case 268:	/* lxvx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 16;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 269:	/* lxvl */
		case 301: {	/* lxvll */
			int nb;
			op->reg = rd | ((instr & 1) << 5);
			op->ea = ra ? regs->gpr[ra] : 0;
			nb = regs->gpr[rb] & 0xff;
			if (nb > 16)
				nb = 16;
			op->type = MKOP(LOAD_VSX, 0, nb);
			op->element_size = 16;
			op->vsx_flags = ((instr & 0x20) ? VSX_LDLEFT : 0) |
				VSX_CHECK_VEC;
			break;
		}
		case 332:	/* lxvdsx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 8);
			op->element_size = 8;
			op->vsx_flags = VSX_SPLAT;
			break;

		case 364:	/* lxvwsx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 4);
			op->element_size = 4;
			op->vsx_flags = VSX_SPLAT | VSX_CHECK_VEC;
			break;

		case 396:	/* stxvx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 16;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 397:	/* stxvl */
		case 429: {	/* stxvll */
			int nb;
			op->reg = rd | ((instr & 1) << 5);
			op->ea = ra ? regs->gpr[ra] : 0;
			nb = regs->gpr[rb] & 0xff;
			if (nb > 16)
				nb = 16;
			op->type = MKOP(STORE_VSX, 0, nb);
			op->element_size = 16;
			op->vsx_flags = ((instr & 0x20) ? VSX_LDLEFT : 0) |
				VSX_CHECK_VEC;
			break;
		}
		case 524:	/* lxsspx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 4);
			op->element_size = 8;
			op->vsx_flags = VSX_FPCONV;
			break;

		case 588:	/* lxsdx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 8);
			op->element_size = 8;
			break;

		case 652:	/* stxsspx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 4);
			op->element_size = 8;
			op->vsx_flags = VSX_FPCONV;
			break;

		case 716:	/* stxsdx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 8);
			op->element_size = 8;
			break;

		case 780:	/* lxvw4x */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 4;
			break;

		case 781:	/* lxsibzx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 1);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 812:	/* lxvh8x */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 2;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 813:	/* lxsihzx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 2);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 844:	/* lxvd2x */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 8;
			break;

		case 876:	/* lxvb16x */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 1;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 908:	/* stxvw4x */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 4;
			break;

		case 909:	/* stxsibx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 1);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 940:	/* stxvh8x */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 2;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 941:	/* stxsihx */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 2);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 972:	/* stxvd2x */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 8;
			break;

		case 1004:	/* stxvb16x */
			op->reg = rd | ((instr & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 1;
			op->vsx_flags = VSX_CHECK_VEC;
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
		op->type = MKOP(LOAD_FP, u, 4);
		op->ea = dform_ea(instr, regs);
		break;

	case 50:	/* lfd */
	case 51:	/* lfdu */
		op->type = MKOP(LOAD_FP, u, 8);
		op->ea = dform_ea(instr, regs);
		break;

	case 52:	/* stfs */
	case 53:	/* stfsu */
		op->type = MKOP(STORE_FP, u, 4);
		op->ea = dform_ea(instr, regs);
		break;

	case 54:	/* stfd */
	case 55:	/* stfdu */
		op->type = MKOP(STORE_FP, u, 8);
		op->ea = dform_ea(instr, regs);
		break;
#endif

#ifdef __powerpc64__
	case 56:	/* lq */
		if (!((rd & 1) || (rd == ra)))
			op->type = MKOP(LOAD, 0, 16);
		op->ea = dqform_ea(instr, regs);
		break;
#endif

#ifdef CONFIG_VSX
	case 57:	/* lxsd, lxssp */
		op->ea = dsform_ea(instr, regs);
		switch (instr & 3) {
		case 2:		/* lxsd */
			op->reg = rd + 32;
			op->type = MKOP(LOAD_VSX, 0, 8);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;
		case 3:		/* lxssp */
			op->reg = rd + 32;
			op->type = MKOP(LOAD_VSX, 0, 4);
			op->element_size = 8;
			op->vsx_flags = VSX_FPCONV | VSX_CHECK_VEC;
			break;
		}
		break;
#endif /* CONFIG_VSX */

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
#endif

#ifdef CONFIG_VSX
	case 61:	/* lxv, stxsd, stxssp, stxv */
		switch (instr & 7) {
		case 1:		/* lxv */
			op->ea = dqform_ea(instr, regs);
			if (instr & 8)
				op->reg = rd + 32;
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 16;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 2:		/* stxsd with LSB of DS field = 0 */
		case 6:		/* stxsd with LSB of DS field = 1 */
			op->ea = dsform_ea(instr, regs);
			op->reg = rd + 32;
			op->type = MKOP(STORE_VSX, 0, 8);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 3:		/* stxssp with LSB of DS field = 0 */
		case 7:		/* stxssp with LSB of DS field = 1 */
			op->ea = dsform_ea(instr, regs);
			op->reg = rd + 32;
			op->type = MKOP(STORE_VSX, 0, 4);
			op->element_size = 8;
			op->vsx_flags = VSX_FPCONV | VSX_CHECK_VEC;
			break;

		case 5:		/* stxv */
			op->ea = dqform_ea(instr, regs);
			if (instr & 8)
				op->reg = rd + 32;
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 16;
			op->vsx_flags = VSX_CHECK_VEC;
			break;
		}
		break;
#endif /* CONFIG_VSX */

#ifdef __powerpc64__
	case 62:	/* std[u] */
		op->ea = dsform_ea(instr, regs);
		switch (instr & 3) {
		case 0:		/* std */
			op->type = MKOP(STORE, 0, 8);
			break;
		case 1:		/* stdu */
			op->type = MKOP(STORE, UPDATE, 8);
			break;
		case 2:		/* stq */
			if (!(rd & 1))
				op->type = MKOP(STORE, 0, 16);
			break;
		}
		break;
#endif /* __powerpc64__ */

	}
	return 0;

 logical_done:
	if (instr & 1)
		set_cr0(regs, op, ra);
 logical_done_nocc:
	op->reg = ra;
	op->type |= SETREG;
	return 1;

 arith_done:
	if (instr & 1)
		set_cr0(regs, op, rd);
 compute_done:
	op->reg = rd;
	op->type |= SETREG;
	return 1;

 priv:
	op->type = INTERRUPT | 0x700;
	op->val = SRR1_PROGPRIV;
	return 0;

 trap:
	op->type = INTERRUPT | 0x700;
	op->val = SRR1_PROGTRAP;
	return 0;
}
EXPORT_SYMBOL_GPL(analyse_instr);
NOKPROBE_SYMBOL(analyse_instr);

/*
 * For PPC32 we always use stwu with r1 to change the stack pointer.
 * So this emulated store may corrupt the exception frame, now we
 * have to provide the exception frame trampoline, which is pushed
 * below the kprobed function stack. So we only update gpr[1] but
 * don't emulate the real store operation. We will do real store
 * operation safely in exception return code by checking this flag.
 */
static nokprobe_inline int handle_stack_update(unsigned long ea, struct pt_regs *regs)
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

static nokprobe_inline void do_signext(unsigned long *valp, int size)
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

static nokprobe_inline void do_byterev(unsigned long *valp, int size)
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
 * Emulate an instruction that can be executed just by updating
 * fields in *regs.
 */
void emulate_update_regs(struct pt_regs *regs, struct instruction_op *op)
{
	unsigned long next_pc;

	next_pc = truncate_if_32bit(regs->msr, regs->nip + 4);
	switch (op->type & INSTR_TYPE_MASK) {
	case COMPUTE:
		if (op->type & SETREG)
			regs->gpr[op->reg] = op->val;
		if (op->type & SETCC)
			regs->ccr = op->ccval;
		if (op->type & SETXER)
			regs->xer = op->xerval;
		break;

	case BRANCH:
		if (op->type & SETLK)
			regs->link = next_pc;
		if (op->type & BRTAKEN)
			next_pc = op->val;
		if (op->type & DECCTR)
			--regs->ctr;
		break;

	case BARRIER:
		switch (op->type & BARRIER_MASK) {
		case BARRIER_SYNC:
			mb();
			break;
		case BARRIER_ISYNC:
			isync();
			break;
		case BARRIER_EIEIO:
			eieio();
			break;
		case BARRIER_LWSYNC:
			asm volatile("lwsync" : : : "memory");
			break;
		case BARRIER_PTESYNC:
			asm volatile("ptesync" : : : "memory");
			break;
		}
		break;

	case MFSPR:
		switch (op->spr) {
		case SPRN_XER:
			regs->gpr[op->reg] = regs->xer & 0xffffffffUL;
			break;
		case SPRN_LR:
			regs->gpr[op->reg] = regs->link;
			break;
		case SPRN_CTR:
			regs->gpr[op->reg] = regs->ctr;
			break;
		default:
			WARN_ON_ONCE(1);
		}
		break;

	case MTSPR:
		switch (op->spr) {
		case SPRN_XER:
			regs->xer = op->val & 0xffffffffUL;
			break;
		case SPRN_LR:
			regs->link = op->val;
			break;
		case SPRN_CTR:
			regs->ctr = op->val;
			break;
		default:
			WARN_ON_ONCE(1);
		}
		break;

	default:
		WARN_ON_ONCE(1);
	}
	regs->nip = next_pc;
}

/*
 * Emulate instructions that cause a transfer of control,
 * loads and stores, and a few other instructions.
 * Returns 1 if the step was emulated, 0 if not,
 * or -1 if the instruction is one that should not be stepped,
 * such as an rfid, or a mtmsrd that would clear MSR_RI.
 */
int emulate_step(struct pt_regs *regs, unsigned int instr)
{
	struct instruction_op op;
	int r, err, size;
	unsigned long val;
	unsigned int cr;
	int i, rd, nb;

	r = analyse_instr(&op, regs, instr);
	if (r < 0)
		return r;
	if (r > 0) {
		emulate_update_regs(regs, &op);
		return 1;
	}

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
		if (!address_ok(regs, op.ea, size))
			return 0;
		err = 0;
		switch (size) {
#ifdef __powerpc64__
		case 1:
			__get_user_asmx(val, op.ea, err, "lbarx");
			break;
		case 2:
			__get_user_asmx(val, op.ea, err, "lharx");
			break;
#endif
		case 4:
			__get_user_asmx(val, op.ea, err, "lwarx");
			break;
#ifdef __powerpc64__
		case 8:
			__get_user_asmx(val, op.ea, err, "ldarx");
			break;
		case 16:
			err = do_lqarx(op.ea, &regs->gpr[op.reg]);
			goto ldst_done;
#endif
		default:
			return 0;
		}
		if (!err)
			regs->gpr[op.reg] = val;
		goto ldst_done;

	case STCX:
		if (op.ea & (size - 1))
			break;		/* can't handle misaligned */
		if (!address_ok(regs, op.ea, size))
			return 0;
		err = 0;
		switch (size) {
#ifdef __powerpc64__
		case 1:
			__put_user_asmx(op.val, op.ea, err, "stbcx.", cr);
			break;
		case 2:
			__put_user_asmx(op.val, op.ea, err, "stbcx.", cr);
			break;
#endif
		case 4:
			__put_user_asmx(op.val, op.ea, err, "stwcx.", cr);
			break;
#ifdef __powerpc64__
		case 8:
			__put_user_asmx(op.val, op.ea, err, "stdcx.", cr);
			break;
		case 16:
			err = do_stqcx(op.ea, regs->gpr[op.reg],
				       regs->gpr[op.reg + 1], &cr);
			break;
#endif
		default:
			return 0;
		}
		if (!err)
			regs->ccr = (regs->ccr & 0x0fffffff) |
				(cr & 0xe0000000) |
				((regs->xer >> 3) & 0x10000000);
		goto ldst_done;

	case LOAD:
#ifdef __powerpc64__
		if (size == 16) {
			err = emulate_lq(regs, op.ea, op.reg);
			goto ldst_done;
		}
#endif
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
		if (!(regs->msr & MSR_FP))
			return 0;
		if (size == 4)
			err = do_fp_load(op.reg, do_lfs, op.ea, size, regs);
		else
			err = do_fp_load(op.reg, do_lfd, op.ea, size, regs);
		goto ldst_done;
#endif
#ifdef CONFIG_ALTIVEC
	case LOAD_VMX:
		if (!(regs->msr & MSR_VEC))
			return 0;
		err = do_vec_load(op.reg, do_lvx, op.ea, regs);
		goto ldst_done;
#endif
#ifdef CONFIG_VSX
	case LOAD_VSX: {
		char mem[16];
		union vsx_reg buf;
		unsigned long msrbit = MSR_VSX;

		/*
		 * Some VSX instructions check the MSR_VEC bit rather than MSR_VSX
		 * when the target of the instruction is a vector register.
		 */
		if (op.reg >= 32 && (op.vsx_flags & VSX_CHECK_VEC))
			msrbit = MSR_VEC;
		if (!(regs->msr & msrbit))
			return 0;
		if (!address_ok(regs, op.ea, size) ||
		    __copy_from_user(mem, (void __user *)op.ea, size))
			return 0;

		emulate_vsx_load(&op, &buf, mem);
		load_vsrn(op.reg, &buf);
		goto ldst_done;
	}
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
#ifdef __powerpc64__
		if (size == 16) {
			err = emulate_stq(regs, op.ea, op.reg);
			goto ldst_done;
		}
#endif
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
		if (!(regs->msr & MSR_FP))
			return 0;
		if (size == 4)
			err = do_fp_store(op.reg, do_stfs, op.ea, size, regs);
		else
			err = do_fp_store(op.reg, do_stfd, op.ea, size, regs);
		goto ldst_done;
#endif
#ifdef CONFIG_ALTIVEC
	case STORE_VMX:
		if (!(regs->msr & MSR_VEC))
			return 0;
		err = do_vec_store(op.reg, do_stvx, op.ea, regs);
		goto ldst_done;
#endif
#ifdef CONFIG_VSX
	case STORE_VSX: {
		char mem[16];
		union vsx_reg buf;
		unsigned long msrbit = MSR_VSX;

		/*
		 * Some VSX instructions check the MSR_VEC bit rather than MSR_VSX
		 * when the target of the instruction is a vector register.
		 */
		if (op.reg >= 32 && (op.vsx_flags & VSX_CHECK_VEC))
			msrbit = MSR_VEC;
		if (!(regs->msr & msrbit))
			return 0;
		if (!address_ok(regs, op.ea, size))
			return 0;

		store_vsrn(op.reg, &buf);
		emulate_vsx_store(&op, &buf, mem);
		if (__copy_to_user((void __user *)op.ea, mem, size))
			return 0;
		goto ldst_done;
	}
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
NOKPROBE_SYMBOL(emulate_step);
