/*
 * Routines to emulate some Altivec/VMX instructions, specifically
 * those that can trap when given denormalized operands in Java mode.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

/* Functions in vector.S */
extern void vaddfp(vector128 *dst, vector128 *a, vector128 *b);
extern void vsubfp(vector128 *dst, vector128 *a, vector128 *b);
extern void vmaddfp(vector128 *dst, vector128 *a, vector128 *b, vector128 *c);
extern void vnmsubfp(vector128 *dst, vector128 *a, vector128 *b, vector128 *c);
extern void vrefp(vector128 *dst, vector128 *src);
extern void vrsqrtefp(vector128 *dst, vector128 *src);
extern void vexptep(vector128 *dst, vector128 *src);

static unsigned int exp2s[8] = {
	0x800000,
	0x8b95c2,
	0x9837f0,
	0xa5fed7,
	0xb504f3,
	0xc5672a,
	0xd744fd,
	0xeac0c7
};

/*
 * Computes an estimate of 2^x.  The `s' argument is the 32-bit
 * single-precision floating-point representation of x.
 */
static unsigned int eexp2(unsigned int s)
{
	int exp, pwr;
	unsigned int mant, frac;

	/* extract exponent field from input */
	exp = ((s >> 23) & 0xff) - 127;
	if (exp > 7) {
		/* check for NaN input */
		if (exp == 128 && (s & 0x7fffff) != 0)
			return s | 0x400000;	/* return QNaN */
		/* 2^-big = 0, 2^+big = +Inf */
		return (s & 0x80000000)? 0: 0x7f800000;	/* 0 or +Inf */
	}
	if (exp < -23)
		return 0x3f800000;	/* 1.0 */

	/* convert to fixed point integer in 9.23 representation */
	pwr = (s & 0x7fffff) | 0x800000;
	if (exp > 0)
		pwr <<= exp;
	else
		pwr >>= -exp;
	if (s & 0x80000000)
		pwr = -pwr;

	/* extract integer part, which becomes exponent part of result */
	exp = (pwr >> 23) + 126;
	if (exp >= 254)
		return 0x7f800000;
	if (exp < -23)
		return 0;

	/* table lookup on top 3 bits of fraction to get mantissa */
	mant = exp2s[(pwr >> 20) & 7];

	/* linear interpolation using remaining 20 bits of fraction */
	asm("mulhwu %0,%1,%2" : "=r" (frac)
	    : "r" (pwr << 12), "r" (0x172b83ff));
	asm("mulhwu %0,%1,%2" : "=r" (frac) : "r" (frac), "r" (mant));
	mant += frac;

	if (exp >= 0)
		return mant + (exp << 23);

	/* denormalized result */
	exp = -exp;
	mant += 1 << (exp - 1);
	return mant >> exp;
}

/*
 * Computes an estimate of log_2(x).  The `s' argument is the 32-bit
 * single-precision floating-point representation of x.
 */
static unsigned int elog2(unsigned int s)
{
	int exp, mant, lz, frac;

	exp = s & 0x7f800000;
	mant = s & 0x7fffff;
	if (exp == 0x7f800000) {	/* Inf or NaN */
		if (mant != 0)
			s |= 0x400000;	/* turn NaN into QNaN */
		return s;
	}
	if ((exp | mant) == 0)		/* +0 or -0 */
		return 0xff800000;	/* return -Inf */

	if (exp == 0) {
		/* denormalized */
		asm("cntlzw %0,%1" : "=r" (lz) : "r" (mant));
		mant <<= lz - 8;
		exp = (-118 - lz) << 23;
	} else {
		mant |= 0x800000;
		exp -= 127 << 23;
	}

	if (mant >= 0xb504f3) {				/* 2^0.5 * 2^23 */
		exp |= 0x400000;			/* 0.5 * 2^23 */
		asm("mulhwu %0,%1,%2" : "=r" (mant)
		    : "r" (mant), "r" (0xb504f334));	/* 2^-0.5 * 2^32 */
	}
	if (mant >= 0x9837f0) {				/* 2^0.25 * 2^23 */
		exp |= 0x200000;			/* 0.25 * 2^23 */
		asm("mulhwu %0,%1,%2" : "=r" (mant)
		    : "r" (mant), "r" (0xd744fccb));	/* 2^-0.25 * 2^32 */
	}
	if (mant >= 0x8b95c2) {				/* 2^0.125 * 2^23 */
		exp |= 0x100000;			/* 0.125 * 2^23 */
		asm("mulhwu %0,%1,%2" : "=r" (mant)
		    : "r" (mant), "r" (0xeac0c6e8));	/* 2^-0.125 * 2^32 */
	}
	if (mant > 0x800000) {				/* 1.0 * 2^23 */
		/* calculate (mant - 1) * 1.381097463 */
		/* 1.381097463 == 0.125 / (2^0.125 - 1) */
		asm("mulhwu %0,%1,%2" : "=r" (frac)
		    : "r" ((mant - 0x800000) << 1), "r" (0xb0c7cd3a));
		exp += frac;
	}
	s = exp & 0x80000000;
	if (exp != 0) {
		if (s)
			exp = -exp;
		asm("cntlzw %0,%1" : "=r" (lz) : "r" (exp));
		lz = 8 - lz;
		if (lz > 0)
			exp >>= lz;
		else if (lz < 0)
			exp <<= -lz;
		s += ((lz + 126) << 23) + exp;
	}
	return s;
}

#define VSCR_SAT	1

static int ctsxs(unsigned int x, int scale, unsigned int *vscrp)
{
	int exp, mant;

	exp = (x >> 23) & 0xff;
	mant = x & 0x7fffff;
	if (exp == 255 && mant != 0)
		return 0;		/* NaN -> 0 */
	exp = exp - 127 + scale;
	if (exp < 0)
		return 0;		/* round towards zero */
	if (exp >= 31) {
		/* saturate, unless the result would be -2^31 */
		if (x + (scale << 23) != 0xcf000000)
			*vscrp |= VSCR_SAT;
		return (x & 0x80000000)? 0x80000000: 0x7fffffff;
	}
	mant |= 0x800000;
	mant = (mant << 7) >> (30 - exp);
	return (x & 0x80000000)? -mant: mant;
}

static unsigned int ctuxs(unsigned int x, int scale, unsigned int *vscrp)
{
	int exp;
	unsigned int mant;

	exp = (x >> 23) & 0xff;
	mant = x & 0x7fffff;
	if (exp == 255 && mant != 0)
		return 0;		/* NaN -> 0 */
	exp = exp - 127 + scale;
	if (exp < 0)
		return 0;		/* round towards zero */
	if (x & 0x80000000) {
		/* negative => saturate to 0 */
		*vscrp |= VSCR_SAT;
		return 0;
	}
	if (exp >= 32) {
		/* saturate */
		*vscrp |= VSCR_SAT;
		return 0xffffffff;
	}
	mant |= 0x800000;
	mant = (mant << 8) >> (31 - exp);
	return mant;
}

/* Round to floating integer, towards 0 */
static unsigned int rfiz(unsigned int x)
{
	int exp;

	exp = ((x >> 23) & 0xff) - 127;
	if (exp == 128 && (x & 0x7fffff) != 0)
		return x | 0x400000;	/* NaN -> make it a QNaN */
	if (exp >= 23)
		return x;		/* it's an integer already (or Inf) */
	if (exp < 0)
		return x & 0x80000000;	/* |x| < 1.0 rounds to 0 */
	return x & ~(0x7fffff >> exp);
}

/* Round to floating integer, towards +/- Inf */
static unsigned int rfii(unsigned int x)
{
	int exp, mask;

	exp = ((x >> 23) & 0xff) - 127;
	if (exp == 128 && (x & 0x7fffff) != 0)
		return x | 0x400000;	/* NaN -> make it a QNaN */
	if (exp >= 23)
		return x;		/* it's an integer already (or Inf) */
	if ((x & 0x7fffffff) == 0)
		return x;		/* +/-0 -> +/-0 */
	if (exp < 0)
		/* 0 < |x| < 1.0 rounds to +/- 1.0 */
		return (x & 0x80000000) | 0x3f800000;
	mask = 0x7fffff >> exp;
	/* mantissa overflows into exponent - that's OK,
	   it can't overflow into the sign bit */
	return (x + mask) & ~mask;
}

/* Round to floating integer, to nearest */
static unsigned int rfin(unsigned int x)
{
	int exp, half;

	exp = ((x >> 23) & 0xff) - 127;
	if (exp == 128 && (x & 0x7fffff) != 0)
		return x | 0x400000;	/* NaN -> make it a QNaN */
	if (exp >= 23)
		return x;		/* it's an integer already (or Inf) */
	if (exp < -1)
		return x & 0x80000000;	/* |x| < 0.5 -> +/-0 */
	if (exp == -1)
		/* 0.5 <= |x| < 1.0 rounds to +/- 1.0 */
		return (x & 0x80000000) | 0x3f800000;
	half = 0x400000 >> exp;
	/* add 0.5 to the magnitude and chop off the fraction bits */
	return (x + half) & ~(0x7fffff >> exp);
}

int emulate_altivec(struct pt_regs *regs)
{
	unsigned int instr, i;
	unsigned int va, vb, vc, vd;
	vector128 *vrs;

	if (get_user(instr, (unsigned int __user *) regs->nip))
		return -EFAULT;
	if ((instr >> 26) != 4)
		return -EINVAL;		/* not an altivec instruction */
	vd = (instr >> 21) & 0x1f;
	va = (instr >> 16) & 0x1f;
	vb = (instr >> 11) & 0x1f;
	vc = (instr >> 6) & 0x1f;

	vrs = current->thread.vr;
	switch (instr & 0x3f) {
	case 10:
		switch (vc) {
		case 0:	/* vaddfp */
			vaddfp(&vrs[vd], &vrs[va], &vrs[vb]);
			break;
		case 1:	/* vsubfp */
			vsubfp(&vrs[vd], &vrs[va], &vrs[vb]);
			break;
		case 4:	/* vrefp */
			vrefp(&vrs[vd], &vrs[vb]);
			break;
		case 5:	/* vrsqrtefp */
			vrsqrtefp(&vrs[vd], &vrs[vb]);
			break;
		case 6:	/* vexptefp */
			for (i = 0; i < 4; ++i)
				vrs[vd].u[i] = eexp2(vrs[vb].u[i]);
			break;
		case 7:	/* vlogefp */
			for (i = 0; i < 4; ++i)
				vrs[vd].u[i] = elog2(vrs[vb].u[i]);
			break;
		case 8:		/* vrfin */
			for (i = 0; i < 4; ++i)
				vrs[vd].u[i] = rfin(vrs[vb].u[i]);
			break;
		case 9:		/* vrfiz */
			for (i = 0; i < 4; ++i)
				vrs[vd].u[i] = rfiz(vrs[vb].u[i]);
			break;
		case 10:	/* vrfip */
			for (i = 0; i < 4; ++i) {
				u32 x = vrs[vb].u[i];
				x = (x & 0x80000000)? rfiz(x): rfii(x);
				vrs[vd].u[i] = x;
			}
			break;
		case 11:	/* vrfim */
			for (i = 0; i < 4; ++i) {
				u32 x = vrs[vb].u[i];
				x = (x & 0x80000000)? rfii(x): rfiz(x);
				vrs[vd].u[i] = x;
			}
			break;
		case 14:	/* vctuxs */
			for (i = 0; i < 4; ++i)
				vrs[vd].u[i] = ctuxs(vrs[vb].u[i], va,
						&current->thread.vscr.u[3]);
			break;
		case 15:	/* vctsxs */
			for (i = 0; i < 4; ++i)
				vrs[vd].u[i] = ctsxs(vrs[vb].u[i], va,
						&current->thread.vscr.u[3]);
			break;
		default:
			return -EINVAL;
		}
		break;
	case 46:	/* vmaddfp */
		vmaddfp(&vrs[vd], &vrs[va], &vrs[vb], &vrs[vc]);
		break;
	case 47:	/* vnmsubfp */
		vnmsubfp(&vrs[vd], &vrs[va], &vrs[vb], &vrs[vc]);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
