// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Single-step support.
 *
 * Copyright (C) 2004 Paul Mackerras <paulus@au.ibm.com>, IBM
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
#include <asm/disassemble.h>

extern char system_call_common[];
extern char system_call_vectored_emulate[];

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
#define XER_OV32	0x00080000U
#define XER_CA32	0x00040000U

#ifdef CONFIG_VSX
#define VSX_REGISTER_XTP(rd)   ((((rd) & 1) << 5) | ((rd) & 0xfe))
#endif

#ifdef CONFIG_PPC_FPU
/*
 * Functions in ldstfp.S
 */
extern void get_fpr(int rn, double *p);
extern void put_fpr(int rn, const double *p);
extern void get_vr(int rn, __vector128 *p);
extern void put_vr(int rn, __vector128 *p);
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

static nokprobe_inline long address_ok(struct pt_regs *regs,
				       unsigned long ea, int nb)
{
	if (!user_mode(regs))
		return 1;
	if (__access_ok(ea, nb))
		return 1;
	if (__access_ok(ea, 1))
		/* Access overlaps the end of the user region */
		regs->dar = TASK_SIZE_MAX - 1;
	else
		regs->dar = ea;
	return 0;
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

	return ea;
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

	return ea;
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

	return ea;
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

	return ea;
}

/*
 * Calculate effective address for a MLS:D-form / 8LS:D-form
 * prefixed instruction
 */
static nokprobe_inline unsigned long mlsd_8lsd_ea(unsigned int instr,
						  unsigned int suffix,
						  const struct pt_regs *regs)
{
	int ra, prefix_r;
	unsigned int  dd;
	unsigned long ea, d0, d1, d;

	prefix_r = GET_PREFIX_R(instr);
	ra = GET_PREFIX_RA(suffix);

	d0 = instr & 0x3ffff;
	d1 = suffix & 0xffff;
	d = (d0 << 16) | d1;

	/*
	 * sign extend a 34 bit number
	 */
	dd = (unsigned int)(d >> 2);
	ea = (signed int)dd;
	ea = (ea << 2) | (d & 0x3);

	if (!prefix_r && ra)
		ea += regs->gpr[ra];
	else if (!prefix_r && !ra)
		; /* Leave ea as is */
	else if (prefix_r)
		ea += regs->nip;

	/*
	 * (prefix_r && ra) is an invalid form. Should already be
	 * checked for by caller!
	 */

	return ea;
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

static nokprobe_inline void do_byte_reverse(void *ptr, int nb)
{
	switch (nb) {
	case 2:
		*(u16 *)ptr = byterev_2(*(u16 *)ptr);
		break;
	case 4:
		*(u32 *)ptr = byterev_4(*(u32 *)ptr);
		break;
#ifdef __powerpc64__
	case 8:
		*(unsigned long *)ptr = byterev_8(*(unsigned long *)ptr);
		break;
	case 16: {
		unsigned long *up = (unsigned long *)ptr;
		unsigned long tmp;
		tmp = byterev_8(up[0]);
		up[0] = byterev_8(up[1]);
		up[1] = tmp;
		break;
	}
	case 32: {
		unsigned long *up = (unsigned long *)ptr;
		unsigned long tmp;

		tmp = byterev_8(up[0]);
		up[0] = byterev_8(up[3]);
		up[3] = tmp;
		tmp = byterev_8(up[2]);
		up[2] = byterev_8(up[1]);
		up[1] = tmp;
		break;
	}

#endif
	default:
		WARN_ON_ONCE(1);
	}
}

static nokprobe_inline int read_mem_aligned(unsigned long *dest,
					    unsigned long ea, int nb,
					    struct pt_regs *regs)
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
	else
		regs->dar = ea;
	return err;
}

/*
 * Copy from userspace to a buffer, using the largest possible
 * aligned accesses, up to sizeof(long).
 */
static nokprobe_inline int copy_mem_in(u8 *dest, unsigned long ea, int nb,
				       struct pt_regs *regs)
{
	int err = 0;
	int c;

	for (; nb > 0; nb -= c) {
		c = max_align(ea);
		if (c > nb)
			c = max_align(nb);
		switch (c) {
		case 1:
			err = __get_user(*dest, (unsigned char __user *) ea);
			break;
		case 2:
			err = __get_user(*(u16 *)dest,
					 (unsigned short __user *) ea);
			break;
		case 4:
			err = __get_user(*(u32 *)dest,
					 (unsigned int __user *) ea);
			break;
#ifdef __powerpc64__
		case 8:
			err = __get_user(*(unsigned long *)dest,
					 (unsigned long __user *) ea);
			break;
#endif
		}
		if (err) {
			regs->dar = ea;
			return err;
		}
		dest += c;
		ea += c;
	}
	return 0;
}

static nokprobe_inline int read_mem_unaligned(unsigned long *dest,
					      unsigned long ea, int nb,
					      struct pt_regs *regs)
{
	union {
		unsigned long ul;
		u8 b[sizeof(unsigned long)];
	} u;
	int i;
	int err;

	u.ul = 0;
	i = IS_BE ? sizeof(unsigned long) - nb : 0;
	err = copy_mem_in(&u.b[i], ea, nb, regs);
	if (!err)
		*dest = u.ul;
	return err;
}

/*
 * Read memory at address ea for nb bytes, return 0 for success
 * or -EFAULT if an error occurred.  N.B. nb must be 1, 2, 4 or 8.
 * If nb < sizeof(long), the result is right-justified on BE systems.
 */
static int read_mem(unsigned long *dest, unsigned long ea, int nb,
			      struct pt_regs *regs)
{
	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	if ((ea & (nb - 1)) == 0)
		return read_mem_aligned(dest, ea, nb, regs);
	return read_mem_unaligned(dest, ea, nb, regs);
}
NOKPROBE_SYMBOL(read_mem);

static nokprobe_inline int write_mem_aligned(unsigned long val,
					     unsigned long ea, int nb,
					     struct pt_regs *regs)
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
	if (err)
		regs->dar = ea;
	return err;
}

/*
 * Copy from a buffer to userspace, using the largest possible
 * aligned accesses, up to sizeof(long).
 */
static nokprobe_inline int copy_mem_out(u8 *dest, unsigned long ea, int nb,
					struct pt_regs *regs)
{
	int err = 0;
	int c;

	for (; nb > 0; nb -= c) {
		c = max_align(ea);
		if (c > nb)
			c = max_align(nb);
		switch (c) {
		case 1:
			err = __put_user(*dest, (unsigned char __user *) ea);
			break;
		case 2:
			err = __put_user(*(u16 *)dest,
					 (unsigned short __user *) ea);
			break;
		case 4:
			err = __put_user(*(u32 *)dest,
					 (unsigned int __user *) ea);
			break;
#ifdef __powerpc64__
		case 8:
			err = __put_user(*(unsigned long *)dest,
					 (unsigned long __user *) ea);
			break;
#endif
		}
		if (err) {
			regs->dar = ea;
			return err;
		}
		dest += c;
		ea += c;
	}
	return 0;
}

static nokprobe_inline int write_mem_unaligned(unsigned long val,
					       unsigned long ea, int nb,
					       struct pt_regs *regs)
{
	union {
		unsigned long ul;
		u8 b[sizeof(unsigned long)];
	} u;
	int i;

	u.ul = val;
	i = IS_BE ? sizeof(unsigned long) - nb : 0;
	return copy_mem_out(&u.b[i], ea, nb, regs);
}

/*
 * Write memory at address ea for nb bytes, return 0 for success
 * or -EFAULT if an error occurred.  N.B. nb must be 1, 2, 4 or 8.
 */
static int write_mem(unsigned long val, unsigned long ea, int nb,
			       struct pt_regs *regs)
{
	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	if ((ea & (nb - 1)) == 0)
		return write_mem_aligned(val, ea, nb, regs);
	return write_mem_unaligned(val, ea, nb, regs);
}
NOKPROBE_SYMBOL(write_mem);

#ifdef CONFIG_PPC_FPU
/*
 * These access either the real FP register or the image in the
 * thread_struct, depending on regs->msr & MSR_FP.
 */
static int do_fp_load(struct instruction_op *op, unsigned long ea,
		      struct pt_regs *regs, bool cross_endian)
{
	int err, rn, nb;
	union {
		int i;
		unsigned int u;
		float f;
		double d[2];
		unsigned long l[2];
		u8 b[2 * sizeof(double)];
	} u;

	nb = GETSIZE(op->type);
	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	rn = op->reg;
	err = copy_mem_in(u.b, ea, nb, regs);
	if (err)
		return err;
	if (unlikely(cross_endian)) {
		do_byte_reverse(u.b, min(nb, 8));
		if (nb == 16)
			do_byte_reverse(&u.b[8], 8);
	}
	preempt_disable();
	if (nb == 4) {
		if (op->type & FPCONV)
			conv_sp_to_dp(&u.f, &u.d[0]);
		else if (op->type & SIGNEXT)
			u.l[0] = u.i;
		else
			u.l[0] = u.u;
	}
	if (regs->msr & MSR_FP)
		put_fpr(rn, &u.d[0]);
	else
		current->thread.TS_FPR(rn) = u.l[0];
	if (nb == 16) {
		/* lfdp */
		rn |= 1;
		if (regs->msr & MSR_FP)
			put_fpr(rn, &u.d[1]);
		else
			current->thread.TS_FPR(rn) = u.l[1];
	}
	preempt_enable();
	return 0;
}
NOKPROBE_SYMBOL(do_fp_load);

static int do_fp_store(struct instruction_op *op, unsigned long ea,
		       struct pt_regs *regs, bool cross_endian)
{
	int rn, nb;
	union {
		unsigned int u;
		float f;
		double d[2];
		unsigned long l[2];
		u8 b[2 * sizeof(double)];
	} u;

	nb = GETSIZE(op->type);
	if (!address_ok(regs, ea, nb))
		return -EFAULT;
	rn = op->reg;
	preempt_disable();
	if (regs->msr & MSR_FP)
		get_fpr(rn, &u.d[0]);
	else
		u.l[0] = current->thread.TS_FPR(rn);
	if (nb == 4) {
		if (op->type & FPCONV)
			conv_dp_to_sp(&u.d[0], &u.f);
		else
			u.u = u.l[0];
	}
	if (nb == 16) {
		rn |= 1;
		if (regs->msr & MSR_FP)
			get_fpr(rn, &u.d[1]);
		else
			u.l[1] = current->thread.TS_FPR(rn);
	}
	preempt_enable();
	if (unlikely(cross_endian)) {
		do_byte_reverse(u.b, min(nb, 8));
		if (nb == 16)
			do_byte_reverse(&u.b[8], 8);
	}
	return copy_mem_out(u.b, ea, nb, regs);
}
NOKPROBE_SYMBOL(do_fp_store);
#endif

#ifdef CONFIG_ALTIVEC
/* For Altivec/VMX, no need to worry about alignment */
static nokprobe_inline int do_vec_load(int rn, unsigned long ea,
				       int size, struct pt_regs *regs,
				       bool cross_endian)
{
	int err;
	union {
		__vector128 v;
		u8 b[sizeof(__vector128)];
	} u = {};

	if (!address_ok(regs, ea & ~0xfUL, 16))
		return -EFAULT;
	/* align to multiple of size */
	ea &= ~(size - 1);
	err = copy_mem_in(&u.b[ea & 0xf], ea, size, regs);
	if (err)
		return err;
	if (unlikely(cross_endian))
		do_byte_reverse(&u.b[ea & 0xf], size);
	preempt_disable();
	if (regs->msr & MSR_VEC)
		put_vr(rn, &u.v);
	else
		current->thread.vr_state.vr[rn] = u.v;
	preempt_enable();
	return 0;
}

static nokprobe_inline int do_vec_store(int rn, unsigned long ea,
					int size, struct pt_regs *regs,
					bool cross_endian)
{
	union {
		__vector128 v;
		u8 b[sizeof(__vector128)];
	} u;

	if (!address_ok(regs, ea & ~0xfUL, 16))
		return -EFAULT;
	/* align to multiple of size */
	ea &= ~(size - 1);

	preempt_disable();
	if (regs->msr & MSR_VEC)
		get_vr(rn, &u.v);
	else
		u.v = current->thread.vr_state.vr[rn];
	preempt_enable();
	if (unlikely(cross_endian))
		do_byte_reverse(&u.b[ea & 0xf], size);
	return copy_mem_out(&u.b[ea & 0xf], ea, size, regs);
}
#endif /* CONFIG_ALTIVEC */

#ifdef __powerpc64__
static nokprobe_inline int emulate_lq(struct pt_regs *regs, unsigned long ea,
				      int reg, bool cross_endian)
{
	int err;

	if (!address_ok(regs, ea, 16))
		return -EFAULT;
	/* if aligned, should be atomic */
	if ((ea & 0xf) == 0) {
		err = do_lq(ea, &regs->gpr[reg]);
	} else {
		err = read_mem(&regs->gpr[reg + IS_LE], ea, 8, regs);
		if (!err)
			err = read_mem(&regs->gpr[reg + IS_BE], ea + 8, 8, regs);
	}
	if (!err && unlikely(cross_endian))
		do_byte_reverse(&regs->gpr[reg], 16);
	return err;
}

static nokprobe_inline int emulate_stq(struct pt_regs *regs, unsigned long ea,
				       int reg, bool cross_endian)
{
	int err;
	unsigned long vals[2];

	if (!address_ok(regs, ea, 16))
		return -EFAULT;
	vals[0] = regs->gpr[reg];
	vals[1] = regs->gpr[reg + 1];
	if (unlikely(cross_endian))
		do_byte_reverse(vals, 16);

	/* if aligned, should be atomic */
	if ((ea & 0xf) == 0)
		return do_stq(ea, vals[0], vals[1]);

	err = write_mem(vals[IS_LE], ea, 8, regs);
	if (!err)
		err = write_mem(vals[IS_BE], ea + 8, 8, regs);
	return err;
}
#endif /* __powerpc64 */

#ifdef CONFIG_VSX
void emulate_vsx_load(struct instruction_op *op, union vsx_reg *reg,
		      const void *mem, bool rev)
{
	int size, read_size;
	int i, j;
	const unsigned int *wp;
	const unsigned short *hp;
	const unsigned char *bp;

	size = GETSIZE(op->type);
	reg->d[0] = reg->d[1] = 0;

	switch (op->element_size) {
	case 32:
		/* [p]lxvp[x] */
	case 16:
		/* whole vector; lxv[x] or lxvl[l] */
		if (size == 0)
			break;
		memcpy(reg, mem, size);
		if (IS_LE && (op->vsx_flags & VSX_LDLEFT))
			rev = !rev;
		if (rev)
			do_byte_reverse(reg, size);
		break;
	case 8:
		/* scalar loads, lxvd2x, lxvdsx */
		read_size = (size >= 8) ? 8 : size;
		i = IS_LE ? 8 : 8 - read_size;
		memcpy(&reg->b[i], mem, read_size);
		if (rev)
			do_byte_reverse(&reg->b[i], 8);
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
			if (size == 16) {
				unsigned long v = *(unsigned long *)(mem + 8);
				reg->d[IS_BE] = !rev ? v : byterev_8(v);
			} else if (op->vsx_flags & VSX_SPLAT)
				reg->d[IS_BE] = reg->d[IS_LE];
		}
		break;
	case 4:
		/* lxvw4x, lxvwsx */
		wp = mem;
		for (j = 0; j < size / 4; ++j) {
			i = IS_LE ? 3 - j : j;
			reg->w[i] = !rev ? *wp++ : byterev_4(*wp++);
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
			reg->h[i] = !rev ? *hp++ : byterev_2(*hp++);
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
		       void *mem, bool rev)
{
	int size, write_size;
	int i, j;
	union vsx_reg buf;
	unsigned int *wp;
	unsigned short *hp;
	unsigned char *bp;

	size = GETSIZE(op->type);

	switch (op->element_size) {
	case 32:
		/* [p]stxvp[x] */
		if (size == 0)
			break;
		if (rev) {
			/* reverse 32 bytes */
			union vsx_reg buf32[2];
			buf32[0].d[0] = byterev_8(reg[1].d[1]);
			buf32[0].d[1] = byterev_8(reg[1].d[0]);
			buf32[1].d[0] = byterev_8(reg[0].d[1]);
			buf32[1].d[1] = byterev_8(reg[0].d[0]);
			memcpy(mem, buf32, size);
		} else {
			memcpy(mem, reg, size);
		}
		break;
	case 16:
		/* stxv, stxvx, stxvl, stxvll */
		if (size == 0)
			break;
		if (IS_LE && (op->vsx_flags & VSX_LDLEFT))
			rev = !rev;
		if (rev) {
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
		if (unlikely(rev)) {
			do_byte_reverse(mem, write_size);
			if (size == 16)
				do_byte_reverse(mem + 8, 8);
		}
		break;
	case 4:
		/* stxvw4x */
		wp = mem;
		for (j = 0; j < size / 4; ++j) {
			i = IS_LE ? 3 - j : j;
			*wp++ = !rev ? reg->w[i] : byterev_4(reg->w[i]);
		}
		break;
	case 2:
		/* stxvh8x */
		hp = mem;
		for (j = 0; j < size / 2; ++j) {
			i = IS_LE ? 7 - j : j;
			*hp++ = !rev ? reg->h[i] : byterev_2(reg->h[i]);
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

static nokprobe_inline int do_vsx_load(struct instruction_op *op,
				       unsigned long ea, struct pt_regs *regs,
				       bool cross_endian)
{
	int reg = op->reg;
	int i, j, nr_vsx_regs;
	u8 mem[32];
	union vsx_reg buf[2];
	int size = GETSIZE(op->type);

	if (!address_ok(regs, ea, size) || copy_mem_in(mem, ea, size, regs))
		return -EFAULT;

	nr_vsx_regs = max(1ul, size / sizeof(__vector128));
	emulate_vsx_load(op, buf, mem, cross_endian);
	preempt_disable();
	if (reg < 32) {
		/* FP regs + extensions */
		if (regs->msr & MSR_FP) {
			for (i = 0; i < nr_vsx_regs; i++) {
				j = IS_LE ? nr_vsx_regs - i - 1 : i;
				load_vsrn(reg + i, &buf[j].v);
			}
		} else {
			for (i = 0; i < nr_vsx_regs; i++) {
				j = IS_LE ? nr_vsx_regs - i - 1 : i;
				current->thread.fp_state.fpr[reg + i][0] = buf[j].d[0];
				current->thread.fp_state.fpr[reg + i][1] = buf[j].d[1];
			}
		}
	} else {
		if (regs->msr & MSR_VEC) {
			for (i = 0; i < nr_vsx_regs; i++) {
				j = IS_LE ? nr_vsx_regs - i - 1 : i;
				load_vsrn(reg + i, &buf[j].v);
			}
		} else {
			for (i = 0; i < nr_vsx_regs; i++) {
				j = IS_LE ? nr_vsx_regs - i - 1 : i;
				current->thread.vr_state.vr[reg - 32 + i] = buf[j].v;
			}
		}
	}
	preempt_enable();
	return 0;
}

static nokprobe_inline int do_vsx_store(struct instruction_op *op,
					unsigned long ea, struct pt_regs *regs,
					bool cross_endian)
{
	int reg = op->reg;
	int i, j, nr_vsx_regs;
	u8 mem[32];
	union vsx_reg buf[2];
	int size = GETSIZE(op->type);

	if (!address_ok(regs, ea, size))
		return -EFAULT;

	nr_vsx_regs = max(1ul, size / sizeof(__vector128));
	preempt_disable();
	if (reg < 32) {
		/* FP regs + extensions */
		if (regs->msr & MSR_FP) {
			for (i = 0; i < nr_vsx_regs; i++) {
				j = IS_LE ? nr_vsx_regs - i - 1 : i;
				store_vsrn(reg + i, &buf[j].v);
			}
		} else {
			for (i = 0; i < nr_vsx_regs; i++) {
				j = IS_LE ? nr_vsx_regs - i - 1 : i;
				buf[j].d[0] = current->thread.fp_state.fpr[reg + i][0];
				buf[j].d[1] = current->thread.fp_state.fpr[reg + i][1];
			}
		}
	} else {
		if (regs->msr & MSR_VEC) {
			for (i = 0; i < nr_vsx_regs; i++) {
				j = IS_LE ? nr_vsx_regs - i - 1 : i;
				store_vsrn(reg + i, &buf[j].v);
			}
		} else {
			for (i = 0; i < nr_vsx_regs; i++) {
				j = IS_LE ? nr_vsx_regs - i - 1 : i;
				buf[j].v = current->thread.vr_state.vr[reg - 32 + i];
			}
		}
	}
	preempt_enable();
	emulate_vsx_store(op, buf, mem, cross_endian);
	return  copy_mem_out(mem, ea, size, regs);
}
#endif /* CONFIG_VSX */

int emulate_dcbz(unsigned long ea, struct pt_regs *regs)
{
	int err;
	unsigned long i, size;

#ifdef __powerpc64__
	size = ppc64_caches.l1d.block_size;
	if (!(regs->msr & MSR_64BIT))
		ea &= 0xffffffffUL;
#else
	size = L1_CACHE_BYTES;
#endif
	ea &= ~(size - 1);
	if (!address_ok(regs, ea, size))
		return -EFAULT;
	for (i = 0; i < size; i += sizeof(long)) {
		err = __put_user(0, (unsigned long __user *) (ea + i));
		if (err) {
			regs->dar = ea;
			return err;
		}
	}
	return 0;
}
NOKPROBE_SYMBOL(emulate_dcbz);

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
				    struct instruction_op *op)
{
	long val = op->val;

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

static nokprobe_inline void set_ca32(struct instruction_op *op, bool val)
{
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		if (val)
			op->xerval |= XER_CA32;
		else
			op->xerval &= ~XER_CA32;
	}
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

	set_ca32(op, (unsigned int)val < (unsigned int)val1 ||
			(carry_in && (unsigned int)val == (unsigned int)val1));
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

	out -= (out >> 1) & 0x5555555555555555ULL;
	out = (0x3333333333333333ULL & out) +
	      (0x3333333333333333ULL & (out >> 2));
	out = (out + (out >> 4)) & 0x0f0f0f0f0f0f0f0fULL;

	if (size == 8) {	/* popcntb */
		op->val = out;
		return;
	}
	out += out >> 8;
	out += out >> 16;
	if (size == 32) {	/* popcntw */
		op->val = out & 0x0000003f0000003fULL;
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
		op->val = res & 0x0000000100000001ULL;
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
		  struct ppc_inst instr)
{
#ifdef CONFIG_PPC64
	unsigned int suffixopcode, prefixtype, prefix_r;
#endif
	unsigned int opcode, ra, rb, rc, rd, spr, u;
	unsigned long int imm;
	unsigned long int val, val2;
	unsigned int mb, me, sh;
	unsigned int word, suffix;
	long ival;

	word = ppc_inst_val(instr);
	suffix = ppc_inst_suffix(instr);

	op->type = COMPUTE;

	opcode = ppc_inst_primary_opcode(instr);
	switch (opcode) {
	case 16:	/* bc */
		op->type = BRANCH;
		imm = (signed short)(word & 0xfffc);
		if ((word & 2) == 0)
			imm += regs->nip;
		op->val = truncate_if_32bit(regs->msr, imm);
		if (word & 1)
			op->type |= SETLK;
		if (branch_taken(word, regs, op))
			op->type |= BRTAKEN;
		return 1;
#ifdef CONFIG_PPC64
	case 17:	/* sc */
		if ((word & 0xfe2) == 2)
			op->type = SYSCALL;
		else if (IS_ENABLED(CONFIG_PPC_BOOK3S_64) &&
				(word & 0xfe3) == 1) {	/* scv */
			op->type = SYSCALL_VECTORED_0;
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
		} else
			op->type = UNKNOWN;
		return 0;
#endif
	case 18:	/* b */
		op->type = BRANCH | BRTAKEN;
		imm = word & 0x03fffffc;
		if (imm & 0x02000000)
			imm -= 0x04000000;
		if ((word & 2) == 0)
			imm += regs->nip;
		op->val = truncate_if_32bit(regs->msr, imm);
		if (word & 1)
			op->type |= SETLK;
		return 1;
	case 19:
		switch ((word >> 1) & 0x3ff) {
		case 0:		/* mcrf */
			op->type = COMPUTE + SETCC;
			rd = 7 - ((word >> 23) & 0x7);
			ra = 7 - ((word >> 18) & 0x7);
			rd *= 4;
			ra *= 4;
			val = (regs->ccr >> ra) & 0xf;
			op->ccval = (regs->ccr & ~(0xfUL << rd)) | (val << rd);
			return 1;

		case 16:	/* bclr */
		case 528:	/* bcctr */
			op->type = BRANCH;
			imm = (word & 0x400)? regs->ctr: regs->link;
			op->val = truncate_if_32bit(regs->msr, imm);
			if (word & 1)
				op->type |= SETLK;
			if (branch_taken(word, regs, op))
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
			ra = (word >> 16) & 0x1f;
			rb = (word >> 11) & 0x1f;
			rd = (word >> 21) & 0x1f;
			ra = (regs->ccr >> (31 - ra)) & 1;
			rb = (regs->ccr >> (31 - rb)) & 1;
			val = (word >> (6 + ra * 2 + rb)) & 1;
			op->ccval = (regs->ccr & ~(1UL << (31 - rd))) |
				(val << (31 - rd));
			return 1;
		}
		break;
	case 31:
		switch ((word >> 1) & 0x3ff) {
		case 598:	/* sync */
			op->type = BARRIER + BARRIER_SYNC;
#ifdef __powerpc64__
			switch ((word >> 21) & 3) {
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

	rd = (word >> 21) & 0x1f;
	ra = (word >> 16) & 0x1f;
	rb = (word >> 11) & 0x1f;
	rc = (word >> 6) & 0x1f;

	switch (opcode) {
#ifdef __powerpc64__
	case 1:
		if (!cpu_has_feature(CPU_FTR_ARCH_31))
			goto unknown_opcode;

		prefix_r = GET_PREFIX_R(word);
		ra = GET_PREFIX_RA(suffix);
		rd = (suffix >> 21) & 0x1f;
		op->reg = rd;
		op->val = regs->gpr[rd];
		suffixopcode = get_op(suffix);
		prefixtype = (word >> 24) & 0x3;
		switch (prefixtype) {
		case 2:
			if (prefix_r && ra)
				return 0;
			switch (suffixopcode) {
			case 14:	/* paddi */
				op->type = COMPUTE | PREFIXED;
				op->val = mlsd_8lsd_ea(word, suffix, regs);
				goto compute_done;
			}
		}
		break;
	case 2:		/* tdi */
		if (rd & trap_compare(regs->gpr[ra], (short) word))
			goto trap;
		return 1;
#endif
	case 3:		/* twi */
		if (rd & trap_compare((int)regs->gpr[ra], (short) word))
			goto trap;
		return 1;

#ifdef __powerpc64__
	case 4:
		/*
		 * There are very many instructions with this primary opcode
		 * introduced in the ISA as early as v2.03. However, the ones
		 * we currently emulate were all introduced with ISA 3.0
		 */
		if (!cpu_has_feature(CPU_FTR_ARCH_300))
			goto unknown_opcode;

		switch (word & 0x3f) {
		case 48:	/* maddhd */
			asm volatile(PPC_MADDHD(%0, %1, %2, %3) :
				     "=r" (op->val) : "r" (regs->gpr[ra]),
				     "r" (regs->gpr[rb]), "r" (regs->gpr[rc]));
			goto compute_done;

		case 49:	/* maddhdu */
			asm volatile(PPC_MADDHDU(%0, %1, %2, %3) :
				     "=r" (op->val) : "r" (regs->gpr[ra]),
				     "r" (regs->gpr[rb]), "r" (regs->gpr[rc]));
			goto compute_done;

		case 51:	/* maddld */
			asm volatile(PPC_MADDLD(%0, %1, %2, %3) :
				     "=r" (op->val) : "r" (regs->gpr[ra]),
				     "r" (regs->gpr[rb]), "r" (regs->gpr[rc]));
			goto compute_done;
		}

		/*
		 * There are other instructions from ISA 3.0 with the same
		 * primary opcode which do not have emulation support yet.
		 */
		goto unknown_opcode;
#endif

	case 7:		/* mulli */
		op->val = regs->gpr[ra] * (short) word;
		goto compute_done;

	case 8:		/* subfic */
		imm = (short) word;
		add_with_carry(regs, op, rd, ~regs->gpr[ra], imm, 1);
		return 1;

	case 10:	/* cmpli */
		imm = (unsigned short) word;
		val = regs->gpr[ra];
#ifdef __powerpc64__
		if ((rd & 1) == 0)
			val = (unsigned int) val;
#endif
		do_cmp_unsigned(regs, op, val, imm, rd >> 2);
		return 1;

	case 11:	/* cmpi */
		imm = (short) word;
		val = regs->gpr[ra];
#ifdef __powerpc64__
		if ((rd & 1) == 0)
			val = (int) val;
#endif
		do_cmp_signed(regs, op, val, imm, rd >> 2);
		return 1;

	case 12:	/* addic */
		imm = (short) word;
		add_with_carry(regs, op, rd, regs->gpr[ra], imm, 0);
		return 1;

	case 13:	/* addic. */
		imm = (short) word;
		add_with_carry(regs, op, rd, regs->gpr[ra], imm, 0);
		set_cr0(regs, op);
		return 1;

	case 14:	/* addi */
		imm = (short) word;
		if (ra)
			imm += regs->gpr[ra];
		op->val = imm;
		goto compute_done;

	case 15:	/* addis */
		imm = ((short) word) << 16;
		if (ra)
			imm += regs->gpr[ra];
		op->val = imm;
		goto compute_done;

	case 19:
		if (((word >> 1) & 0x1f) == 2) {
			/* addpcis */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			imm = (short) (word & 0xffc1);	/* d0 + d2 fields */
			imm |= (word >> 15) & 0x3e;	/* d1 field */
			op->val = regs->nip + (imm << 16) + 4;
			goto compute_done;
		}
		op->type = UNKNOWN;
		return 0;

	case 20:	/* rlwimi */
		mb = (word >> 6) & 0x1f;
		me = (word >> 1) & 0x1f;
		val = DATA32(regs->gpr[rd]);
		imm = MASK32(mb, me);
		op->val = (regs->gpr[ra] & ~imm) | (ROTATE(val, rb) & imm);
		goto logical_done;

	case 21:	/* rlwinm */
		mb = (word >> 6) & 0x1f;
		me = (word >> 1) & 0x1f;
		val = DATA32(regs->gpr[rd]);
		op->val = ROTATE(val, rb) & MASK32(mb, me);
		goto logical_done;

	case 23:	/* rlwnm */
		mb = (word >> 6) & 0x1f;
		me = (word >> 1) & 0x1f;
		rb = regs->gpr[rb] & 0x1f;
		val = DATA32(regs->gpr[rd]);
		op->val = ROTATE(val, rb) & MASK32(mb, me);
		goto logical_done;

	case 24:	/* ori */
		op->val = regs->gpr[rd] | (unsigned short) word;
		goto logical_done_nocc;

	case 25:	/* oris */
		imm = (unsigned short) word;
		op->val = regs->gpr[rd] | (imm << 16);
		goto logical_done_nocc;

	case 26:	/* xori */
		op->val = regs->gpr[rd] ^ (unsigned short) word;
		goto logical_done_nocc;

	case 27:	/* xoris */
		imm = (unsigned short) word;
		op->val = regs->gpr[rd] ^ (imm << 16);
		goto logical_done_nocc;

	case 28:	/* andi. */
		op->val = regs->gpr[rd] & (unsigned short) word;
		set_cr0(regs, op);
		goto logical_done_nocc;

	case 29:	/* andis. */
		imm = (unsigned short) word;
		op->val = regs->gpr[rd] & (imm << 16);
		set_cr0(regs, op);
		goto logical_done_nocc;

#ifdef __powerpc64__
	case 30:	/* rld* */
		mb = ((word >> 6) & 0x1f) | (word & 0x20);
		val = regs->gpr[rd];
		if ((word & 0x10) == 0) {
			sh = rb | ((word & 2) << 4);
			val = ROTATE(val, sh);
			switch ((word >> 2) & 3) {
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
			switch ((word >> 1) & 7) {
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
		/* isel occupies 32 minor opcodes */
		if (((word >> 1) & 0x1f) == 15) {
			mb = (word >> 6) & 0x1f; /* bc field */
			val = (regs->ccr >> (31 - mb)) & 1;
			val2 = (ra) ? regs->gpr[ra] : 0;

			op->val = (val) ? val2 : regs->gpr[rb];
			goto compute_done;
		}

		switch ((word >> 1) & 0x3ff) {
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
			imm = (word & 0x10000)? 0x8002: 0xefffffffffffeffeUL;
			op->val = imm;
			return 0;
#endif

		case 19:	/* mfcr */
			imm = 0xffffffffUL;
			if ((word >> 20) & 1) {
				imm = 0xf0000000UL;
				for (sh = 0; sh < 8; ++sh) {
					if (word & (0x80000 >> sh))
						break;
					imm >>= 4;
				}
			}
			op->val = regs->ccr & imm;
			goto compute_done;

		case 128:	/* setb */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			/*
			 * 'ra' encodes the CR field number (bfa) in the top 3 bits.
			 * Since each CR field is 4 bits,
			 * we can simply mask off the bottom two bits (bfa * 4)
			 * to yield the first bit in the CR field.
			 */
			ra = ra & ~0x3;
			/* 'val' stores bits of the CR field (bfa) */
			val = regs->ccr >> (CR0_SHIFT - ra);
			/* checks if the LT bit of CR field (bfa) is set */
			if (val & 8)
				op->val = -1;
			/* checks if the GT bit of CR field (bfa) is set */
			else if (val & 4)
				op->val = 1;
			else
				op->val = 0;
			goto compute_done;

		case 144:	/* mtcrf */
			op->type = COMPUTE + SETCC;
			imm = 0xf0000000UL;
			val = regs->gpr[rd];
			op->ccval = regs->ccr;
			for (sh = 0; sh < 8; ++sh) {
				if (word & (0x80000 >> sh))
					op->ccval = (op->ccval & ~imm) |
						(val & imm);
				imm >>= 4;
			}
			return 1;

		case 339:	/* mfspr */
			spr = ((word >> 16) & 0x1f) | ((word >> 6) & 0x3e0);
			op->type = MFSPR;
			op->reg = rd;
			op->spr = spr;
			if (spr == SPRN_XER || spr == SPRN_LR ||
			    spr == SPRN_CTR)
				return 1;
			return 0;

		case 467:	/* mtspr */
			spr = ((word >> 16) & 0x1f) | ((word >> 6) & 0x3e0);
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
			op->val = (long)(int) regs->gpr[ra] *
				(int) regs->gpr[rb];

			goto arith_done;
#ifdef __powerpc64__
		case 265:	/* modud */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->val = regs->gpr[ra] % regs->gpr[rb];
			goto compute_done;
#endif
		case 266:	/* add */
			op->val = regs->gpr[ra] + regs->gpr[rb];
			goto arith_done;

		case 267:	/* moduw */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->val = (unsigned int) regs->gpr[ra] %
				(unsigned int) regs->gpr[rb];
			goto compute_done;
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
#ifdef __powerpc64__
		case 425:	/* divde[.] */
			asm volatile(PPC_DIVDE(%0, %1, %2) :
				"=r" (op->val) : "r" (regs->gpr[ra]),
				"r" (regs->gpr[rb]));
			goto arith_done;
		case 393:	/* divdeu[.] */
			asm volatile(PPC_DIVDEU(%0, %1, %2) :
				"=r" (op->val) : "r" (regs->gpr[ra]),
				"r" (regs->gpr[rb]));
			goto arith_done;
#endif
		case 755:	/* darn */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			switch (ra & 0x3) {
			case 0:
				/* 32-bit conditioned */
				asm volatile(PPC_DARN(%0, 0) : "=r" (op->val));
				goto compute_done;

			case 1:
				/* 64-bit conditioned */
				asm volatile(PPC_DARN(%0, 1) : "=r" (op->val));
				goto compute_done;

			case 2:
				/* 64-bit raw */
				asm volatile(PPC_DARN(%0, 2) : "=r" (op->val));
				goto compute_done;
			}

			goto unknown_opcode;
#ifdef __powerpc64__
		case 777:	/* modsd */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->val = (long int) regs->gpr[ra] %
				(long int) regs->gpr[rb];
			goto compute_done;
#endif
		case 779:	/* modsw */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->val = (int) regs->gpr[ra] %
				(int) regs->gpr[rb];
			goto compute_done;


/*
 * Logical instructions
 */
		case 26:	/* cntlzw */
			val = (unsigned int) regs->gpr[rd];
			op->val = ( val ? __builtin_clz(val) : 32 );
			goto logical_done;
#ifdef __powerpc64__
		case 58:	/* cntlzd */
			val = regs->gpr[rd];
			op->val = ( val ? __builtin_clzl(val) : 64 );
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
			goto logical_done_nocc;

		case 124:	/* nor */
			op->val = ~(regs->gpr[rd] | regs->gpr[rb]);
			goto logical_done;

		case 154:	/* prtyw */
			do_prty(regs, op, regs->gpr[rd], 32);
			goto logical_done_nocc;

		case 186:	/* prtyd */
			do_prty(regs, op, regs->gpr[rd], 64);
			goto logical_done_nocc;
#ifdef CONFIG_PPC64
		case 252:	/* bpermd */
			do_bpermd(regs, op, regs->gpr[rd], regs->gpr[rb]);
			goto logical_done_nocc;
#endif
		case 284:	/* xor */
			op->val = ~(regs->gpr[rd] ^ regs->gpr[rb]);
			goto logical_done;

		case 316:	/* xor */
			op->val = regs->gpr[rd] ^ regs->gpr[rb];
			goto logical_done;

		case 378:	/* popcntw */
			do_popcnt(regs, op, regs->gpr[rd], 32);
			goto logical_done_nocc;

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
			goto logical_done_nocc;
#endif
		case 538:	/* cnttzw */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			val = (unsigned int) regs->gpr[rd];
			op->val = (val ? __builtin_ctz(val) : 32);
			goto logical_done;
#ifdef __powerpc64__
		case 570:	/* cnttzd */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			val = regs->gpr[rd];
			op->val = (val ? __builtin_ctzl(val) : 64);
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
			set_ca32(op, op->xerval & XER_CA);
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
			set_ca32(op, op->xerval & XER_CA);
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
			set_ca32(op, op->xerval & XER_CA);
			goto logical_done;

		case 826:	/* sradi with sh_5 = 0 */
		case 827:	/* sradi with sh_5 = 1 */
			op->type = COMPUTE + SETREG + SETXER;
			sh = rb | ((word & 2) << 4);
			ival = (signed long int) regs->gpr[rd];
			op->val = ival >> sh;
			op->xerval = regs->xer;
			if (ival < 0 && (ival & ((1ul << sh) - 1)) != 0)
				op->xerval |= XER_CA;
			else
				op->xerval &= ~XER_CA;
			set_ca32(op, op->xerval & XER_CA);
			goto logical_done;

		case 890:	/* extswsli with sh_5 = 0 */
		case 891:	/* extswsli with sh_5 = 1 */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->type = COMPUTE + SETREG;
			sh = rb | ((word & 2) << 4);
			val = (signed int) regs->gpr[rd];
			if (sh)
				op->val = ROTATE(val, sh) & MASK64(0, 63 - sh);
			else
				op->val = val;
			goto logical_done;

#endif /* __powerpc64__ */

/*
 * Cache instructions
 */
		case 54:	/* dcbst */
			op->type = MKOP(CACHEOP, DCBST, 0);
			op->ea = xform_ea(word, regs);
			return 0;

		case 86:	/* dcbf */
			op->type = MKOP(CACHEOP, DCBF, 0);
			op->ea = xform_ea(word, regs);
			return 0;

		case 246:	/* dcbtst */
			op->type = MKOP(CACHEOP, DCBTST, 0);
			op->ea = xform_ea(word, regs);
			op->reg = rd;
			return 0;

		case 278:	/* dcbt */
			op->type = MKOP(CACHEOP, DCBTST, 0);
			op->ea = xform_ea(word, regs);
			op->reg = rd;
			return 0;

		case 982:	/* icbi */
			op->type = MKOP(CACHEOP, ICBI, 0);
			op->ea = xform_ea(word, regs);
			return 0;

		case 1014:	/* dcbz */
			op->type = MKOP(CACHEOP, DCBZ, 0);
			op->ea = xform_ea(word, regs);
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
	u = (word >> 20) & UPDATE;
	op->vsx_flags = 0;

	switch (opcode) {
	case 31:
		u = word & UPDATE;
		op->ea = xform_ea(word, regs);
		switch ((word >> 1) & 0x3ff) {
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
		/*
		 * Note: for the load/store vector element instructions,
		 * bits of the EA say which field of the VMX register to use.
		 */
		case 7:		/* lvebx */
			op->type = MKOP(LOAD_VMX, 0, 1);
			op->element_size = 1;
			break;

		case 39:	/* lvehx */
			op->type = MKOP(LOAD_VMX, 0, 2);
			op->element_size = 2;
			break;

		case 71:	/* lvewx */
			op->type = MKOP(LOAD_VMX, 0, 4);
			op->element_size = 4;
			break;

		case 103:	/* lvx */
		case 359:	/* lvxl */
			op->type = MKOP(LOAD_VMX, 0, 16);
			op->element_size = 16;
			break;

		case 135:	/* stvebx */
			op->type = MKOP(STORE_VMX, 0, 1);
			op->element_size = 1;
			break;

		case 167:	/* stvehx */
			op->type = MKOP(STORE_VMX, 0, 2);
			op->element_size = 2;
			break;

		case 199:	/* stvewx */
			op->type = MKOP(STORE_VMX, 0, 4);
			op->element_size = 4;
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
			op->ea = ra ? regs->gpr[ra] : 0;
			break;

#ifdef CONFIG_PPC_FPU
		case 535:	/* lfsx */
		case 567:	/* lfsux */
			op->type = MKOP(LOAD_FP, u | FPCONV, 4);
			break;

		case 599:	/* lfdx */
		case 631:	/* lfdux */
			op->type = MKOP(LOAD_FP, u, 8);
			break;

		case 663:	/* stfsx */
		case 695:	/* stfsux */
			op->type = MKOP(STORE_FP, u | FPCONV, 4);
			break;

		case 727:	/* stfdx */
		case 759:	/* stfdux */
			op->type = MKOP(STORE_FP, u, 8);
			break;

#ifdef __powerpc64__
		case 791:	/* lfdpx */
			op->type = MKOP(LOAD_FP, 0, 16);
			break;

		case 855:	/* lfiwax */
			op->type = MKOP(LOAD_FP, SIGNEXT, 4);
			break;

		case 887:	/* lfiwzx */
			op->type = MKOP(LOAD_FP, 0, 4);
			break;

		case 919:	/* stfdpx */
			op->type = MKOP(STORE_FP, 0, 16);
			break;

		case 983:	/* stfiwx */
			op->type = MKOP(STORE_FP, 0, 4);
			break;
#endif /* __powerpc64 */
#endif /* CONFIG_PPC_FPU */

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

		case 725:	/* stswi */
			if (rb == 0)
				rb = 32;	/* # bytes to store */
			op->type = MKOP(STORE_MULTI, 0, rb);
			op->ea = ra ? regs->gpr[ra] : 0;
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
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 4);
			op->element_size = 8;
			break;

		case 76:	/* lxsiwax */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, SIGNEXT, 4);
			op->element_size = 8;
			break;

		case 140:	/* stxsiwx */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 4);
			op->element_size = 8;
			break;

		case 268:	/* lxvx */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 16;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 269:	/* lxvl */
		case 301: {	/* lxvll */
			int nb;
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->ea = ra ? regs->gpr[ra] : 0;
			nb = regs->gpr[rb] & 0xff;
			if (nb > 16)
				nb = 16;
			op->type = MKOP(LOAD_VSX, 0, nb);
			op->element_size = 16;
			op->vsx_flags = ((word & 0x20) ? VSX_LDLEFT : 0) |
				VSX_CHECK_VEC;
			break;
		}
		case 332:	/* lxvdsx */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 8);
			op->element_size = 8;
			op->vsx_flags = VSX_SPLAT;
			break;

		case 333:       /* lxvpx */
			if (!cpu_has_feature(CPU_FTR_ARCH_31))
				goto unknown_opcode;
			op->reg = VSX_REGISTER_XTP(rd);
			op->type = MKOP(LOAD_VSX, 0, 32);
			op->element_size = 32;
			break;

		case 364:	/* lxvwsx */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 4);
			op->element_size = 4;
			op->vsx_flags = VSX_SPLAT | VSX_CHECK_VEC;
			break;

		case 396:	/* stxvx */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 16;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 397:	/* stxvl */
		case 429: {	/* stxvll */
			int nb;
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->ea = ra ? regs->gpr[ra] : 0;
			nb = regs->gpr[rb] & 0xff;
			if (nb > 16)
				nb = 16;
			op->type = MKOP(STORE_VSX, 0, nb);
			op->element_size = 16;
			op->vsx_flags = ((word & 0x20) ? VSX_LDLEFT : 0) |
				VSX_CHECK_VEC;
			break;
		}
		case 461:       /* stxvpx */
			if (!cpu_has_feature(CPU_FTR_ARCH_31))
				goto unknown_opcode;
			op->reg = VSX_REGISTER_XTP(rd);
			op->type = MKOP(STORE_VSX, 0, 32);
			op->element_size = 32;
			break;
		case 524:	/* lxsspx */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 4);
			op->element_size = 8;
			op->vsx_flags = VSX_FPCONV;
			break;

		case 588:	/* lxsdx */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 8);
			op->element_size = 8;
			break;

		case 652:	/* stxsspx */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 4);
			op->element_size = 8;
			op->vsx_flags = VSX_FPCONV;
			break;

		case 716:	/* stxsdx */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 8);
			op->element_size = 8;
			break;

		case 780:	/* lxvw4x */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 4;
			break;

		case 781:	/* lxsibzx */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 1);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 812:	/* lxvh8x */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 2;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 813:	/* lxsihzx */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 2);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 844:	/* lxvd2x */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 8;
			break;

		case 876:	/* lxvb16x */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 1;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 908:	/* stxvw4x */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 4;
			break;

		case 909:	/* stxsibx */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 1);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 940:	/* stxvh8x */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 2;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 941:	/* stxsihx */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 2);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 972:	/* stxvd2x */
			op->reg = rd | ((word & 1) << 5);
			op->type = MKOP(STORE_VSX, 0, 16);
			op->element_size = 8;
			break;

		case 1004:	/* stxvb16x */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd | ((word & 1) << 5);
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
		op->ea = dform_ea(word, regs);
		break;

	case 34:	/* lbz */
	case 35:	/* lbzu */
		op->type = MKOP(LOAD, u, 1);
		op->ea = dform_ea(word, regs);
		break;

	case 36:	/* stw */
	case 37:	/* stwu */
		op->type = MKOP(STORE, u, 4);
		op->ea = dform_ea(word, regs);
		break;

	case 38:	/* stb */
	case 39:	/* stbu */
		op->type = MKOP(STORE, u, 1);
		op->ea = dform_ea(word, regs);
		break;

	case 40:	/* lhz */
	case 41:	/* lhzu */
		op->type = MKOP(LOAD, u, 2);
		op->ea = dform_ea(word, regs);
		break;

	case 42:	/* lha */
	case 43:	/* lhau */
		op->type = MKOP(LOAD, SIGNEXT | u, 2);
		op->ea = dform_ea(word, regs);
		break;

	case 44:	/* sth */
	case 45:	/* sthu */
		op->type = MKOP(STORE, u, 2);
		op->ea = dform_ea(word, regs);
		break;

	case 46:	/* lmw */
		if (ra >= rd)
			break;		/* invalid form, ra in range to load */
		op->type = MKOP(LOAD_MULTI, 0, 4 * (32 - rd));
		op->ea = dform_ea(word, regs);
		break;

	case 47:	/* stmw */
		op->type = MKOP(STORE_MULTI, 0, 4 * (32 - rd));
		op->ea = dform_ea(word, regs);
		break;

#ifdef CONFIG_PPC_FPU
	case 48:	/* lfs */
	case 49:	/* lfsu */
		op->type = MKOP(LOAD_FP, u | FPCONV, 4);
		op->ea = dform_ea(word, regs);
		break;

	case 50:	/* lfd */
	case 51:	/* lfdu */
		op->type = MKOP(LOAD_FP, u, 8);
		op->ea = dform_ea(word, regs);
		break;

	case 52:	/* stfs */
	case 53:	/* stfsu */
		op->type = MKOP(STORE_FP, u | FPCONV, 4);
		op->ea = dform_ea(word, regs);
		break;

	case 54:	/* stfd */
	case 55:	/* stfdu */
		op->type = MKOP(STORE_FP, u, 8);
		op->ea = dform_ea(word, regs);
		break;
#endif

#ifdef __powerpc64__
	case 56:	/* lq */
		if (!((rd & 1) || (rd == ra)))
			op->type = MKOP(LOAD, 0, 16);
		op->ea = dqform_ea(word, regs);
		break;
#endif

#ifdef CONFIG_VSX
	case 57:	/* lfdp, lxsd, lxssp */
		op->ea = dsform_ea(word, regs);
		switch (word & 3) {
		case 0:		/* lfdp */
			if (rd & 1)
				break;		/* reg must be even */
			op->type = MKOP(LOAD_FP, 0, 16);
			break;
		case 2:		/* lxsd */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->reg = rd + 32;
			op->type = MKOP(LOAD_VSX, 0, 8);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;
		case 3:		/* lxssp */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
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
		op->ea = dsform_ea(word, regs);
		switch (word & 3) {
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
	case 6:
		if (!cpu_has_feature(CPU_FTR_ARCH_31))
			goto unknown_opcode;
		op->ea = dqform_ea(word, regs);
		op->reg = VSX_REGISTER_XTP(rd);
		op->element_size = 32;
		switch (word & 0xf) {
		case 0:         /* lxvp */
			op->type = MKOP(LOAD_VSX, 0, 32);
			break;
		case 1:         /* stxvp */
			op->type = MKOP(STORE_VSX, 0, 32);
			break;
		}
		break;

	case 61:	/* stfdp, lxv, stxsd, stxssp, stxv */
		switch (word & 7) {
		case 0:		/* stfdp with LSB of DS field = 0 */
		case 4:		/* stfdp with LSB of DS field = 1 */
			op->ea = dsform_ea(word, regs);
			op->type = MKOP(STORE_FP, 0, 16);
			break;

		case 1:		/* lxv */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->ea = dqform_ea(word, regs);
			if (word & 8)
				op->reg = rd + 32;
			op->type = MKOP(LOAD_VSX, 0, 16);
			op->element_size = 16;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 2:		/* stxsd with LSB of DS field = 0 */
		case 6:		/* stxsd with LSB of DS field = 1 */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->ea = dsform_ea(word, regs);
			op->reg = rd + 32;
			op->type = MKOP(STORE_VSX, 0, 8);
			op->element_size = 8;
			op->vsx_flags = VSX_CHECK_VEC;
			break;

		case 3:		/* stxssp with LSB of DS field = 0 */
		case 7:		/* stxssp with LSB of DS field = 1 */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->ea = dsform_ea(word, regs);
			op->reg = rd + 32;
			op->type = MKOP(STORE_VSX, 0, 4);
			op->element_size = 8;
			op->vsx_flags = VSX_FPCONV | VSX_CHECK_VEC;
			break;

		case 5:		/* stxv */
			if (!cpu_has_feature(CPU_FTR_ARCH_300))
				goto unknown_opcode;
			op->ea = dqform_ea(word, regs);
			if (word & 8)
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
		op->ea = dsform_ea(word, regs);
		switch (word & 3) {
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
	case 1: /* Prefixed instructions */
		if (!cpu_has_feature(CPU_FTR_ARCH_31))
			goto unknown_opcode;

		prefix_r = GET_PREFIX_R(word);
		ra = GET_PREFIX_RA(suffix);
		op->update_reg = ra;
		rd = (suffix >> 21) & 0x1f;
		op->reg = rd;
		op->val = regs->gpr[rd];

		suffixopcode = get_op(suffix);
		prefixtype = (word >> 24) & 0x3;
		switch (prefixtype) {
		case 0: /* Type 00  Eight-Byte Load/Store */
			if (prefix_r && ra)
				break;
			op->ea = mlsd_8lsd_ea(word, suffix, regs);
			switch (suffixopcode) {
			case 41:	/* plwa */
				op->type = MKOP(LOAD, PREFIXED | SIGNEXT, 4);
				break;
#ifdef CONFIG_VSX
			case 42:        /* plxsd */
				op->reg = rd + 32;
				op->type = MKOP(LOAD_VSX, PREFIXED, 8);
				op->element_size = 8;
				op->vsx_flags = VSX_CHECK_VEC;
				break;
			case 43:	/* plxssp */
				op->reg = rd + 32;
				op->type = MKOP(LOAD_VSX, PREFIXED, 4);
				op->element_size = 8;
				op->vsx_flags = VSX_FPCONV | VSX_CHECK_VEC;
				break;
			case 46:	/* pstxsd */
				op->reg = rd + 32;
				op->type = MKOP(STORE_VSX, PREFIXED, 8);
				op->element_size = 8;
				op->vsx_flags = VSX_CHECK_VEC;
				break;
			case 47:	/* pstxssp */
				op->reg = rd + 32;
				op->type = MKOP(STORE_VSX, PREFIXED, 4);
				op->element_size = 8;
				op->vsx_flags = VSX_FPCONV | VSX_CHECK_VEC;
				break;
			case 51:	/* plxv1 */
				op->reg += 32;
				fallthrough;
			case 50:	/* plxv0 */
				op->type = MKOP(LOAD_VSX, PREFIXED, 16);
				op->element_size = 16;
				op->vsx_flags = VSX_CHECK_VEC;
				break;
			case 55:	/* pstxv1 */
				op->reg = rd + 32;
				fallthrough;
			case 54:	/* pstxv0 */
				op->type = MKOP(STORE_VSX, PREFIXED, 16);
				op->element_size = 16;
				op->vsx_flags = VSX_CHECK_VEC;
				break;
#endif /* CONFIG_VSX */
			case 56:        /* plq */
				op->type = MKOP(LOAD, PREFIXED, 16);
				break;
			case 57:	/* pld */
				op->type = MKOP(LOAD, PREFIXED, 8);
				break;
#ifdef CONFIG_VSX
			case 58:        /* plxvp */
				op->reg = VSX_REGISTER_XTP(rd);
				op->type = MKOP(LOAD_VSX, PREFIXED, 32);
				op->element_size = 32;
				break;
#endif /* CONFIG_VSX */
			case 60:        /* pstq */
				op->type = MKOP(STORE, PREFIXED, 16);
				break;
			case 61:	/* pstd */
				op->type = MKOP(STORE, PREFIXED, 8);
				break;
#ifdef CONFIG_VSX
			case 62:        /* pstxvp */
				op->reg = VSX_REGISTER_XTP(rd);
				op->type = MKOP(STORE_VSX, PREFIXED, 32);
				op->element_size = 32;
				break;
#endif /* CONFIG_VSX */
			}
			break;
		case 1: /* Type 01 Eight-Byte Register-to-Register */
			break;
		case 2: /* Type 10 Modified Load/Store */
			if (prefix_r && ra)
				break;
			op->ea = mlsd_8lsd_ea(word, suffix, regs);
			switch (suffixopcode) {
			case 32:	/* plwz */
				op->type = MKOP(LOAD, PREFIXED, 4);
				break;
			case 34:	/* plbz */
				op->type = MKOP(LOAD, PREFIXED, 1);
				break;
			case 36:	/* pstw */
				op->type = MKOP(STORE, PREFIXED, 4);
				break;
			case 38:	/* pstb */
				op->type = MKOP(STORE, PREFIXED, 1);
				break;
			case 40:	/* plhz */
				op->type = MKOP(LOAD, PREFIXED, 2);
				break;
			case 42:	/* plha */
				op->type = MKOP(LOAD, PREFIXED | SIGNEXT, 2);
				break;
			case 44:	/* psth */
				op->type = MKOP(STORE, PREFIXED, 2);
				break;
			case 48:        /* plfs */
				op->type = MKOP(LOAD_FP, PREFIXED | FPCONV, 4);
				break;
			case 50:        /* plfd */
				op->type = MKOP(LOAD_FP, PREFIXED, 8);
				break;
			case 52:        /* pstfs */
				op->type = MKOP(STORE_FP, PREFIXED | FPCONV, 4);
				break;
			case 54:        /* pstfd */
				op->type = MKOP(STORE_FP, PREFIXED, 8);
				break;
			}
			break;
		case 3: /* Type 11 Modified Register-to-Register */
			break;
		}
#endif /* __powerpc64__ */

	}

	if (OP_IS_LOAD_STORE(op->type) && (op->type & UPDATE)) {
		switch (GETTYPE(op->type)) {
		case LOAD:
			if (ra == rd)
				goto unknown_opcode;
			fallthrough;
		case STORE:
		case LOAD_FP:
		case STORE_FP:
			if (ra == 0)
				goto unknown_opcode;
		}
	}

#ifdef CONFIG_VSX
	if ((GETTYPE(op->type) == LOAD_VSX ||
	     GETTYPE(op->type) == STORE_VSX) &&
	    !cpu_has_feature(CPU_FTR_VSX)) {
		return -1;
	}
#endif /* CONFIG_VSX */

	return 0;

 unknown_opcode:
	op->type = UNKNOWN;
	return 0;

 logical_done:
	if (word & 1)
		set_cr0(regs, op);
 logical_done_nocc:
	op->reg = ra;
	op->type |= SETREG;
	return 1;

 arith_done:
	if (word & 1)
		set_cr0(regs, op);
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

	next_pc = truncate_if_32bit(regs->msr, regs->nip + GETLENGTH(op->type));
	switch (GETTYPE(op->type)) {
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
#ifdef CONFIG_PPC64
		case BARRIER_LWSYNC:
			asm volatile("lwsync" : : : "memory");
			break;
		case BARRIER_PTESYNC:
			asm volatile("ptesync" : : : "memory");
			break;
#endif
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
	regs_set_return_ip(regs, next_pc);
}
NOKPROBE_SYMBOL(emulate_update_regs);

/*
 * Emulate a previously-analysed load or store instruction.
 * Return values are:
 * 0 = instruction emulated successfully
 * -EFAULT = address out of range or access faulted (regs->dar
 *	     contains the faulting address)
 * -EACCES = misaligned access, instruction requires alignment
 * -EINVAL = unknown operation in *op
 */
int emulate_loadstore(struct pt_regs *regs, struct instruction_op *op)
{
	int err, size, type;
	int i, rd, nb;
	unsigned int cr;
	unsigned long val;
	unsigned long ea;
	bool cross_endian;

	err = 0;
	size = GETSIZE(op->type);
	type = GETTYPE(op->type);
	cross_endian = (regs->msr & MSR_LE) != (MSR_KERNEL & MSR_LE);
	ea = truncate_if_32bit(regs->msr, op->ea);

	switch (type) {
	case LARX:
		if (ea & (size - 1))
			return -EACCES;		/* can't handle misaligned */
		if (!address_ok(regs, ea, size))
			return -EFAULT;
		err = 0;
		val = 0;
		switch (size) {
#ifdef __powerpc64__
		case 1:
			__get_user_asmx(val, ea, err, "lbarx");
			break;
		case 2:
			__get_user_asmx(val, ea, err, "lharx");
			break;
#endif
		case 4:
			__get_user_asmx(val, ea, err, "lwarx");
			break;
#ifdef __powerpc64__
		case 8:
			__get_user_asmx(val, ea, err, "ldarx");
			break;
		case 16:
			err = do_lqarx(ea, &regs->gpr[op->reg]);
			break;
#endif
		default:
			return -EINVAL;
		}
		if (err) {
			regs->dar = ea;
			break;
		}
		if (size < 16)
			regs->gpr[op->reg] = val;
		break;

	case STCX:
		if (ea & (size - 1))
			return -EACCES;		/* can't handle misaligned */
		if (!address_ok(regs, ea, size))
			return -EFAULT;
		err = 0;
		switch (size) {
#ifdef __powerpc64__
		case 1:
			__put_user_asmx(op->val, ea, err, "stbcx.", cr);
			break;
		case 2:
			__put_user_asmx(op->val, ea, err, "stbcx.", cr);
			break;
#endif
		case 4:
			__put_user_asmx(op->val, ea, err, "stwcx.", cr);
			break;
#ifdef __powerpc64__
		case 8:
			__put_user_asmx(op->val, ea, err, "stdcx.", cr);
			break;
		case 16:
			err = do_stqcx(ea, regs->gpr[op->reg],
				       regs->gpr[op->reg + 1], &cr);
			break;
#endif
		default:
			return -EINVAL;
		}
		if (!err)
			regs->ccr = (regs->ccr & 0x0fffffff) |
				(cr & 0xe0000000) |
				((regs->xer >> 3) & 0x10000000);
		else
			regs->dar = ea;
		break;

	case LOAD:
#ifdef __powerpc64__
		if (size == 16) {
			err = emulate_lq(regs, ea, op->reg, cross_endian);
			break;
		}
#endif
		err = read_mem(&regs->gpr[op->reg], ea, size, regs);
		if (!err) {
			if (op->type & SIGNEXT)
				do_signext(&regs->gpr[op->reg], size);
			if ((op->type & BYTEREV) == (cross_endian ? 0 : BYTEREV))
				do_byterev(&regs->gpr[op->reg], size);
		}
		break;

#ifdef CONFIG_PPC_FPU
	case LOAD_FP:
		/*
		 * If the instruction is in userspace, we can emulate it even
		 * if the VMX state is not live, because we have the state
		 * stored in the thread_struct.  If the instruction is in
		 * the kernel, we must not touch the state in the thread_struct.
		 */
		if (!(regs->msr & MSR_PR) && !(regs->msr & MSR_FP))
			return 0;
		err = do_fp_load(op, ea, regs, cross_endian);
		break;
#endif
#ifdef CONFIG_ALTIVEC
	case LOAD_VMX:
		if (!(regs->msr & MSR_PR) && !(regs->msr & MSR_VEC))
			return 0;
		err = do_vec_load(op->reg, ea, size, regs, cross_endian);
		break;
#endif
#ifdef CONFIG_VSX
	case LOAD_VSX: {
		unsigned long msrbit = MSR_VSX;

		/*
		 * Some VSX instructions check the MSR_VEC bit rather than MSR_VSX
		 * when the target of the instruction is a vector register.
		 */
		if (op->reg >= 32 && (op->vsx_flags & VSX_CHECK_VEC))
			msrbit = MSR_VEC;
		if (!(regs->msr & MSR_PR) && !(regs->msr & msrbit))
			return 0;
		err = do_vsx_load(op, ea, regs, cross_endian);
		break;
	}
#endif
	case LOAD_MULTI:
		if (!address_ok(regs, ea, size))
			return -EFAULT;
		rd = op->reg;
		for (i = 0; i < size; i += 4) {
			unsigned int v32 = 0;

			nb = size - i;
			if (nb > 4)
				nb = 4;
			err = copy_mem_in((u8 *) &v32, ea, nb, regs);
			if (err)
				break;
			if (unlikely(cross_endian))
				v32 = byterev_4(v32);
			regs->gpr[rd] = v32;
			ea += 4;
			/* reg number wraps from 31 to 0 for lsw[ix] */
			rd = (rd + 1) & 0x1f;
		}
		break;

	case STORE:
#ifdef __powerpc64__
		if (size == 16) {
			err = emulate_stq(regs, ea, op->reg, cross_endian);
			break;
		}
#endif
		if ((op->type & UPDATE) && size == sizeof(long) &&
		    op->reg == 1 && op->update_reg == 1 &&
		    !(regs->msr & MSR_PR) &&
		    ea >= regs->gpr[1] - STACK_INT_FRAME_SIZE) {
			err = handle_stack_update(ea, regs);
			break;
		}
		if (unlikely(cross_endian))
			do_byterev(&op->val, size);
		err = write_mem(op->val, ea, size, regs);
		break;

#ifdef CONFIG_PPC_FPU
	case STORE_FP:
		if (!(regs->msr & MSR_PR) && !(regs->msr & MSR_FP))
			return 0;
		err = do_fp_store(op, ea, regs, cross_endian);
		break;
#endif
#ifdef CONFIG_ALTIVEC
	case STORE_VMX:
		if (!(regs->msr & MSR_PR) && !(regs->msr & MSR_VEC))
			return 0;
		err = do_vec_store(op->reg, ea, size, regs, cross_endian);
		break;
#endif
#ifdef CONFIG_VSX
	case STORE_VSX: {
		unsigned long msrbit = MSR_VSX;

		/*
		 * Some VSX instructions check the MSR_VEC bit rather than MSR_VSX
		 * when the target of the instruction is a vector register.
		 */
		if (op->reg >= 32 && (op->vsx_flags & VSX_CHECK_VEC))
			msrbit = MSR_VEC;
		if (!(regs->msr & MSR_PR) && !(regs->msr & msrbit))
			return 0;
		err = do_vsx_store(op, ea, regs, cross_endian);
		break;
	}
#endif
	case STORE_MULTI:
		if (!address_ok(regs, ea, size))
			return -EFAULT;
		rd = op->reg;
		for (i = 0; i < size; i += 4) {
			unsigned int v32 = regs->gpr[rd];

			nb = size - i;
			if (nb > 4)
				nb = 4;
			if (unlikely(cross_endian))
				v32 = byterev_4(v32);
			err = copy_mem_out((u8 *) &v32, ea, nb, regs);
			if (err)
				break;
			ea += 4;
			/* reg number wraps from 31 to 0 for stsw[ix] */
			rd = (rd + 1) & 0x1f;
		}
		break;

	default:
		return -EINVAL;
	}

	if (err)
		return err;

	if (op->type & UPDATE)
		regs->gpr[op->update_reg] = op->ea;

	return 0;
}
NOKPROBE_SYMBOL(emulate_loadstore);

/*
 * Emulate instructions that cause a transfer of control,
 * loads and stores, and a few other instructions.
 * Returns 1 if the step was emulated, 0 if not,
 * or -1 if the instruction is one that should not be stepped,
 * such as an rfid, or a mtmsrd that would clear MSR_RI.
 */
int emulate_step(struct pt_regs *regs, struct ppc_inst instr)
{
	struct instruction_op op;
	int r, err, type;
	unsigned long val;
	unsigned long ea;

	r = analyse_instr(&op, regs, instr);
	if (r < 0)
		return r;
	if (r > 0) {
		emulate_update_regs(regs, &op);
		return 1;
	}

	err = 0;
	type = GETTYPE(op.type);

	if (OP_IS_LOAD_STORE(type)) {
		err = emulate_loadstore(regs, &op);
		if (err)
			return 0;
		goto instr_done;
	}

	switch (type) {
	case CACHEOP:
		ea = truncate_if_32bit(regs->msr, op.ea);
		if (!address_ok(regs, ea, 8))
			return 0;
		switch (op.type & CACHEOP_MASK) {
		case DCBST:
			__cacheop_user_asmx(ea, err, "dcbst");
			break;
		case DCBF:
			__cacheop_user_asmx(ea, err, "dcbf");
			break;
		case DCBTST:
			if (op.reg == 0)
				prefetchw((void *) ea);
			break;
		case DCBT:
			if (op.reg == 0)
				prefetch((void *) ea);
			break;
		case ICBI:
			__cacheop_user_asmx(ea, err, "icbi");
			break;
		case DCBZ:
			err = emulate_dcbz(ea, regs);
			break;
		}
		if (err) {
			regs->dar = ea;
			return 0;
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
		regs_set_return_msr(regs, (regs->msr & ~op.val) | (val & op.val));
		goto instr_done;

#ifdef CONFIG_PPC64
	case SYSCALL:	/* sc */
		/*
		 * N.B. this uses knowledge about how the syscall
		 * entry code works.  If that is changed, this will
		 * need to be changed also.
		 */
		if (IS_ENABLED(CONFIG_PPC_FAST_ENDIAN_SWITCH) &&
				cpu_has_feature(CPU_FTR_REAL_LE) &&
				regs->gpr[0] == 0x1ebe) {
			regs_set_return_msr(regs, regs->msr ^ MSR_LE);
			goto instr_done;
		}
		regs->gpr[9] = regs->gpr[13];
		regs->gpr[10] = MSR_KERNEL;
		regs->gpr[11] = regs->nip + 4;
		regs->gpr[12] = regs->msr & MSR_MASK;
		regs->gpr[13] = (unsigned long) get_paca();
		regs_set_return_ip(regs, (unsigned long) &system_call_common);
		regs_set_return_msr(regs, MSR_KERNEL);
		return 1;

#ifdef CONFIG_PPC_BOOK3S_64
	case SYSCALL_VECTORED_0:	/* scv 0 */
		regs->gpr[9] = regs->gpr[13];
		regs->gpr[10] = MSR_KERNEL;
		regs->gpr[11] = regs->nip + 4;
		regs->gpr[12] = regs->msr & MSR_MASK;
		regs->gpr[13] = (unsigned long) get_paca();
		regs_set_return_ip(regs, (unsigned long) &system_call_vectored_emulate);
		regs_set_return_msr(regs, MSR_KERNEL);
		return 1;
#endif

	case RFI:
		return -1;
#endif
	}
	return 0;

 instr_done:
	regs_set_return_ip(regs,
		truncate_if_32bit(regs->msr, regs->nip + GETLENGTH(op.type)));
	return 1;
}
NOKPROBE_SYMBOL(emulate_step);
