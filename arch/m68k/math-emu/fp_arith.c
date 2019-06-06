// SPDX-License-Identifier: GPL-2.0-or-later
/*

   fp_arith.c: floating-point math routines for the Linux-m68k
   floating point emulator.

   Copyright (c) 1998-1999 David Huggins-Daines.

   Somewhat based on the AlphaLinux floating point emulator, by David
   Mosberger-Tang.

 */

#include "fp_emu.h"
#include "multi_arith.h"
#include "fp_arith.h"

const struct fp_ext fp_QNaN =
{
	.exp = 0x7fff,
	.mant = { .m64 = ~0 }
};

const struct fp_ext fp_Inf =
{
	.exp = 0x7fff,
};

/* let's start with the easy ones */

struct fp_ext *
fp_fabs(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "fabs\n");

	fp_monadic_check(dest, src);

	dest->sign = 0;

	return dest;
}

struct fp_ext *
fp_fneg(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "fneg\n");

	fp_monadic_check(dest, src);

	dest->sign = !dest->sign;

	return dest;
}

/* Now, the slightly harder ones */

/* fp_fadd: Implements the kernel of the FADD, FSADD, FDADD, FSUB,
   FDSUB, and FCMP instructions. */

struct fp_ext *
fp_fadd(struct fp_ext *dest, struct fp_ext *src)
{
	int diff;

	dprint(PINSTR, "fadd\n");

	fp_dyadic_check(dest, src);

	if (IS_INF(dest)) {
		/* infinity - infinity == NaN */
		if (IS_INF(src) && (src->sign != dest->sign))
			fp_set_nan(dest);
		return dest;
	}
	if (IS_INF(src)) {
		fp_copy_ext(dest, src);
		return dest;
	}

	if (IS_ZERO(dest)) {
		if (IS_ZERO(src)) {
			if (src->sign != dest->sign) {
				if (FPDATA->rnd == FPCR_ROUND_RM)
					dest->sign = 1;
				else
					dest->sign = 0;
			}
		} else
			fp_copy_ext(dest, src);
		return dest;
	}

	dest->lowmant = src->lowmant = 0;

	if ((diff = dest->exp - src->exp) > 0)
		fp_denormalize(src, diff);
	else if ((diff = -diff) > 0)
		fp_denormalize(dest, diff);

	if (dest->sign == src->sign) {
		if (fp_addmant(dest, src))
			if (!fp_addcarry(dest))
				return dest;
	} else {
		if (dest->mant.m64 < src->mant.m64) {
			fp_submant(dest, src, dest);
			dest->sign = !dest->sign;
		} else
			fp_submant(dest, dest, src);
	}

	return dest;
}

/* fp_fsub: Implements the kernel of the FSUB, FSSUB, and FDSUB
   instructions.

   Remember that the arguments are in assembler-syntax order! */

struct fp_ext *
fp_fsub(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "fsub ");

	src->sign = !src->sign;
	return fp_fadd(dest, src);
}


struct fp_ext *
fp_fcmp(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "fcmp ");

	FPDATA->temp[1] = *dest;
	src->sign = !src->sign;
	return fp_fadd(&FPDATA->temp[1], src);
}

struct fp_ext *
fp_ftst(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "ftst\n");

	(void)dest;

	return src;
}

struct fp_ext *
fp_fmul(struct fp_ext *dest, struct fp_ext *src)
{
	union fp_mant128 temp;
	int exp;

	dprint(PINSTR, "fmul\n");

	fp_dyadic_check(dest, src);

	/* calculate the correct sign now, as it's necessary for infinities */
	dest->sign = src->sign ^ dest->sign;

	/* Handle infinities */
	if (IS_INF(dest)) {
		if (IS_ZERO(src))
			fp_set_nan(dest);
		return dest;
	}
	if (IS_INF(src)) {
		if (IS_ZERO(dest))
			fp_set_nan(dest);
		else
			fp_copy_ext(dest, src);
		return dest;
	}

	/* Of course, as we all know, zero * anything = zero.  You may
	   not have known that it might be a positive or negative
	   zero... */
	if (IS_ZERO(dest) || IS_ZERO(src)) {
		dest->exp = 0;
		dest->mant.m64 = 0;
		dest->lowmant = 0;

		return dest;
	}

	exp = dest->exp + src->exp - 0x3ffe;

	/* shift up the mantissa for denormalized numbers,
	   so that the highest bit is set, this makes the
	   shift of the result below easier */
	if ((long)dest->mant.m32[0] >= 0)
		exp -= fp_overnormalize(dest);
	if ((long)src->mant.m32[0] >= 0)
		exp -= fp_overnormalize(src);

	/* now, do a 64-bit multiply with expansion */
	fp_multiplymant(&temp, dest, src);

	/* normalize it back to 64 bits and stuff it back into the
	   destination struct */
	if ((long)temp.m32[0] > 0) {
		exp--;
		fp_putmant128(dest, &temp, 1);
	} else
		fp_putmant128(dest, &temp, 0);

	if (exp >= 0x7fff) {
		fp_set_ovrflw(dest);
		return dest;
	}
	dest->exp = exp;
	if (exp < 0) {
		fp_set_sr(FPSR_EXC_UNFL);
		fp_denormalize(dest, -exp);
	}

	return dest;
}

/* fp_fdiv: Implements the "kernel" of the FDIV, FSDIV, FDDIV and
   FSGLDIV instructions.

   Note that the order of the operands is counter-intuitive: instead
   of src / dest, the result is actually dest / src. */

struct fp_ext *
fp_fdiv(struct fp_ext *dest, struct fp_ext *src)
{
	union fp_mant128 temp;
	int exp;

	dprint(PINSTR, "fdiv\n");

	fp_dyadic_check(dest, src);

	/* calculate the correct sign now, as it's necessary for infinities */
	dest->sign = src->sign ^ dest->sign;

	/* Handle infinities */
	if (IS_INF(dest)) {
		/* infinity / infinity = NaN (quiet, as always) */
		if (IS_INF(src))
			fp_set_nan(dest);
		/* infinity / anything else = infinity (with approprate sign) */
		return dest;
	}
	if (IS_INF(src)) {
		/* anything / infinity = zero (with appropriate sign) */
		dest->exp = 0;
		dest->mant.m64 = 0;
		dest->lowmant = 0;

		return dest;
	}

	/* zeroes */
	if (IS_ZERO(dest)) {
		/* zero / zero = NaN */
		if (IS_ZERO(src))
			fp_set_nan(dest);
		/* zero / anything else = zero */
		return dest;
	}
	if (IS_ZERO(src)) {
		/* anything / zero = infinity (with appropriate sign) */
		fp_set_sr(FPSR_EXC_DZ);
		dest->exp = 0x7fff;
		dest->mant.m64 = 0;

		return dest;
	}

	exp = dest->exp - src->exp + 0x3fff;

	/* shift up the mantissa for denormalized numbers,
	   so that the highest bit is set, this makes lots
	   of things below easier */
	if ((long)dest->mant.m32[0] >= 0)
		exp -= fp_overnormalize(dest);
	if ((long)src->mant.m32[0] >= 0)
		exp -= fp_overnormalize(src);

	/* now, do the 64-bit divide */
	fp_dividemant(&temp, dest, src);

	/* normalize it back to 64 bits and stuff it back into the
	   destination struct */
	if (!temp.m32[0]) {
		exp--;
		fp_putmant128(dest, &temp, 32);
	} else
		fp_putmant128(dest, &temp, 31);

	if (exp >= 0x7fff) {
		fp_set_ovrflw(dest);
		return dest;
	}
	dest->exp = exp;
	if (exp < 0) {
		fp_set_sr(FPSR_EXC_UNFL);
		fp_denormalize(dest, -exp);
	}

	return dest;
}

struct fp_ext *
fp_fsglmul(struct fp_ext *dest, struct fp_ext *src)
{
	int exp;

	dprint(PINSTR, "fsglmul\n");

	fp_dyadic_check(dest, src);

	/* calculate the correct sign now, as it's necessary for infinities */
	dest->sign = src->sign ^ dest->sign;

	/* Handle infinities */
	if (IS_INF(dest)) {
		if (IS_ZERO(src))
			fp_set_nan(dest);
		return dest;
	}
	if (IS_INF(src)) {
		if (IS_ZERO(dest))
			fp_set_nan(dest);
		else
			fp_copy_ext(dest, src);
		return dest;
	}

	/* Of course, as we all know, zero * anything = zero.  You may
	   not have known that it might be a positive or negative
	   zero... */
	if (IS_ZERO(dest) || IS_ZERO(src)) {
		dest->exp = 0;
		dest->mant.m64 = 0;
		dest->lowmant = 0;

		return dest;
	}

	exp = dest->exp + src->exp - 0x3ffe;

	/* do a 32-bit multiply */
	fp_mul64(dest->mant.m32[0], dest->mant.m32[1],
		 dest->mant.m32[0] & 0xffffff00,
		 src->mant.m32[0] & 0xffffff00);

	if (exp >= 0x7fff) {
		fp_set_ovrflw(dest);
		return dest;
	}
	dest->exp = exp;
	if (exp < 0) {
		fp_set_sr(FPSR_EXC_UNFL);
		fp_denormalize(dest, -exp);
	}

	return dest;
}

struct fp_ext *
fp_fsgldiv(struct fp_ext *dest, struct fp_ext *src)
{
	int exp;
	unsigned long quot, rem;

	dprint(PINSTR, "fsgldiv\n");

	fp_dyadic_check(dest, src);

	/* calculate the correct sign now, as it's necessary for infinities */
	dest->sign = src->sign ^ dest->sign;

	/* Handle infinities */
	if (IS_INF(dest)) {
		/* infinity / infinity = NaN (quiet, as always) */
		if (IS_INF(src))
			fp_set_nan(dest);
		/* infinity / anything else = infinity (with approprate sign) */
		return dest;
	}
	if (IS_INF(src)) {
		/* anything / infinity = zero (with appropriate sign) */
		dest->exp = 0;
		dest->mant.m64 = 0;
		dest->lowmant = 0;

		return dest;
	}

	/* zeroes */
	if (IS_ZERO(dest)) {
		/* zero / zero = NaN */
		if (IS_ZERO(src))
			fp_set_nan(dest);
		/* zero / anything else = zero */
		return dest;
	}
	if (IS_ZERO(src)) {
		/* anything / zero = infinity (with appropriate sign) */
		fp_set_sr(FPSR_EXC_DZ);
		dest->exp = 0x7fff;
		dest->mant.m64 = 0;

		return dest;
	}

	exp = dest->exp - src->exp + 0x3fff;

	dest->mant.m32[0] &= 0xffffff00;
	src->mant.m32[0] &= 0xffffff00;

	/* do the 32-bit divide */
	if (dest->mant.m32[0] >= src->mant.m32[0]) {
		fp_sub64(dest->mant, src->mant);
		fp_div64(quot, rem, dest->mant.m32[0], 0, src->mant.m32[0]);
		dest->mant.m32[0] = 0x80000000 | (quot >> 1);
		dest->mant.m32[1] = (quot & 1) | rem;	/* only for rounding */
	} else {
		fp_div64(quot, rem, dest->mant.m32[0], 0, src->mant.m32[0]);
		dest->mant.m32[0] = quot;
		dest->mant.m32[1] = rem;		/* only for rounding */
		exp--;
	}

	if (exp >= 0x7fff) {
		fp_set_ovrflw(dest);
		return dest;
	}
	dest->exp = exp;
	if (exp < 0) {
		fp_set_sr(FPSR_EXC_UNFL);
		fp_denormalize(dest, -exp);
	}

	return dest;
}

/* fp_roundint: Internal rounding function for use by several of these
   emulated instructions.

   This one rounds off the fractional part using the rounding mode
   specified. */

static void fp_roundint(struct fp_ext *dest, int mode)
{
	union fp_mant64 oldmant;
	unsigned long mask;

	if (!fp_normalize_ext(dest))
		return;

	/* infinities and zeroes */
	if (IS_INF(dest) || IS_ZERO(dest))
		return;

	/* first truncate the lower bits */
	oldmant = dest->mant;
	switch (dest->exp) {
	case 0 ... 0x3ffe:
		dest->mant.m64 = 0;
		break;
	case 0x3fff ... 0x401e:
		dest->mant.m32[0] &= 0xffffffffU << (0x401e - dest->exp);
		dest->mant.m32[1] = 0;
		if (oldmant.m64 == dest->mant.m64)
			return;
		break;
	case 0x401f ... 0x403e:
		dest->mant.m32[1] &= 0xffffffffU << (0x403e - dest->exp);
		if (oldmant.m32[1] == dest->mant.m32[1])
			return;
		break;
	default:
		return;
	}
	fp_set_sr(FPSR_EXC_INEX2);

	/* We might want to normalize upwards here... however, since
	   we know that this is only called on the output of fp_fdiv,
	   or with the input to fp_fint or fp_fintrz, and the inputs
	   to all these functions are either normal or denormalized
	   (no subnormals allowed!), there's really no need.

	   In the case of fp_fdiv, observe that 0x80000000 / 0xffff =
	   0xffff8000, and the same holds for 128-bit / 64-bit. (i.e. the
	   smallest possible normal dividend and the largest possible normal
	   divisor will still produce a normal quotient, therefore, (normal
	   << 64) / normal is normal in all cases) */

	switch (mode) {
	case FPCR_ROUND_RN:
		switch (dest->exp) {
		case 0 ... 0x3ffd:
			return;
		case 0x3ffe:
			/* As noted above, the input is always normal, so the
			   guard bit (bit 63) is always set.  therefore, the
			   only case in which we will NOT round to 1.0 is when
			   the input is exactly 0.5. */
			if (oldmant.m64 == (1ULL << 63))
				return;
			break;
		case 0x3fff ... 0x401d:
			mask = 1 << (0x401d - dest->exp);
			if (!(oldmant.m32[0] & mask))
				return;
			if (oldmant.m32[0] & (mask << 1))
				break;
			if (!(oldmant.m32[0] << (dest->exp - 0x3ffd)) &&
					!oldmant.m32[1])
				return;
			break;
		case 0x401e:
			if (oldmant.m32[1] & 0x80000000)
				return;
			if (oldmant.m32[0] & 1)
				break;
			if (!(oldmant.m32[1] << 1))
				return;
			break;
		case 0x401f ... 0x403d:
			mask = 1 << (0x403d - dest->exp);
			if (!(oldmant.m32[1] & mask))
				return;
			if (oldmant.m32[1] & (mask << 1))
				break;
			if (!(oldmant.m32[1] << (dest->exp - 0x401d)))
				return;
			break;
		default:
			return;
		}
		break;
	case FPCR_ROUND_RZ:
		return;
	default:
		if (dest->sign ^ (mode - FPCR_ROUND_RM))
			break;
		return;
	}

	switch (dest->exp) {
	case 0 ... 0x3ffe:
		dest->exp = 0x3fff;
		dest->mant.m64 = 1ULL << 63;
		break;
	case 0x3fff ... 0x401e:
		mask = 1 << (0x401e - dest->exp);
		if (dest->mant.m32[0] += mask)
			break;
		dest->mant.m32[0] = 0x80000000;
		dest->exp++;
		break;
	case 0x401f ... 0x403e:
		mask = 1 << (0x403e - dest->exp);
		if (dest->mant.m32[1] += mask)
			break;
		if (dest->mant.m32[0] += 1)
                        break;
		dest->mant.m32[0] = 0x80000000;
                dest->exp++;
		break;
	}
}

/* modrem_kernel: Implementation of the FREM and FMOD instructions
   (which are exactly the same, except for the rounding used on the
   intermediate value) */

static struct fp_ext *
modrem_kernel(struct fp_ext *dest, struct fp_ext *src, int mode)
{
	struct fp_ext tmp;

	fp_dyadic_check(dest, src);

	/* Infinities and zeros */
	if (IS_INF(dest) || IS_ZERO(src)) {
		fp_set_nan(dest);
		return dest;
	}
	if (IS_ZERO(dest) || IS_INF(src))
		return dest;

	/* FIXME: there is almost certainly a smarter way to do this */
	fp_copy_ext(&tmp, dest);
	fp_fdiv(&tmp, src);		/* NOTE: src might be modified */
	fp_roundint(&tmp, mode);
	fp_fmul(&tmp, src);
	fp_fsub(dest, &tmp);

	/* set the quotient byte */
	fp_set_quotient((dest->mant.m64 & 0x7f) | (dest->sign << 7));
	return dest;
}

/* fp_fmod: Implements the kernel of the FMOD instruction.

   Again, the argument order is backwards.  The result, as defined in
   the Motorola manuals, is:

   fmod(src,dest) = (dest - (src * floor(dest / src))) */

struct fp_ext *
fp_fmod(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "fmod\n");
	return modrem_kernel(dest, src, FPCR_ROUND_RZ);
}

/* fp_frem: Implements the kernel of the FREM instruction.

   frem(src,dest) = (dest - (src * round(dest / src)))
 */

struct fp_ext *
fp_frem(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "frem\n");
	return modrem_kernel(dest, src, FPCR_ROUND_RN);
}

struct fp_ext *
fp_fint(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "fint\n");

	fp_copy_ext(dest, src);

	fp_roundint(dest, FPDATA->rnd);

	return dest;
}

struct fp_ext *
fp_fintrz(struct fp_ext *dest, struct fp_ext *src)
{
	dprint(PINSTR, "fintrz\n");

	fp_copy_ext(dest, src);

	fp_roundint(dest, FPCR_ROUND_RZ);

	return dest;
}

struct fp_ext *
fp_fscale(struct fp_ext *dest, struct fp_ext *src)
{
	int scale, oldround;

	dprint(PINSTR, "fscale\n");

	fp_dyadic_check(dest, src);

	/* Infinities */
	if (IS_INF(src)) {
		fp_set_nan(dest);
		return dest;
	}
	if (IS_INF(dest))
		return dest;

	/* zeroes */
	if (IS_ZERO(src) || IS_ZERO(dest))
		return dest;

	/* Source exponent out of range */
	if (src->exp >= 0x400c) {
		fp_set_ovrflw(dest);
		return dest;
	}

	/* src must be rounded with round to zero. */
	oldround = FPDATA->rnd;
	FPDATA->rnd = FPCR_ROUND_RZ;
	scale = fp_conv_ext2long(src);
	FPDATA->rnd = oldround;

	/* new exponent */
	scale += dest->exp;

	if (scale >= 0x7fff) {
		fp_set_ovrflw(dest);
	} else if (scale <= 0) {
		fp_set_sr(FPSR_EXC_UNFL);
		fp_denormalize(dest, -scale);
	} else
		dest->exp = scale;

	return dest;
}

