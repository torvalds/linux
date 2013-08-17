/*
 *  linux/arch/arm/vfp/vfpdouble.c
 *
 * This code is derived in part from John R. Housers softfloat library, which
 * carries the following notice:
 *
 * ===========================================================================
 * This C source file is part of the SoftFloat IEC/IEEE Floating-point
 * Arithmetic Package, Release 2.
 *
 * Written by John R. Hauser.  This work was made possible in part by the
 * International Computer Science Institute, located at Suite 600, 1947 Center
 * Street, Berkeley, California 94704.  Funding was partially provided by the
 * National Science Foundation under grant MIP-9311980.  The original version
 * of this code was written as part of a project to build a fixed-point vector
 * processor in collaboration with the University of California at Berkeley,
 * overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
 * is available through the web page `http://HTTP.CS.Berkeley.EDU/~jhauser/
 * arithmetic/softfloat.html'.
 *
 * THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
 * has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
 * TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
 * PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
 * AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.
 *
 * Derivative works are acceptable, even for commercial purposes, so long as
 * (1) they include prominent notice that the work is derivative, and (2) they
 * include prominent notice akin to these three paragraphs for those parts of
 * this code that are retained.
 * ===========================================================================
 */
#include <linux/kernel.h>
#include <linux/bitops.h>

#include <asm/div64.h>
#include <asm/vfp.h>

#include "vfpinstr.h"
#include "vfp.h"

static struct vfp_double vfp_double_default_qnan = {
	.exponent	= 2047,
	.sign		= 0,
	.significand	= VFP_DOUBLE_SIGNIFICAND_QNAN,
};

static void vfp_double_dump(const char *str, struct vfp_double *d)
{
	pr_debug("VFP: %s: sign=%d exponent=%d significand=%016llx\n",
		 str, d->sign != 0, d->exponent, d->significand);
}

static void vfp_double_normalise_denormal(struct vfp_double *vd)
{
	int bits = 31 - fls(vd->significand >> 32);
	if (bits == 31)
		bits = 63 - fls(vd->significand);

	vfp_double_dump("normalise_denormal: in", vd);

	if (bits) {
		vd->exponent -= bits - 1;
		vd->significand <<= bits;
	}

	vfp_double_dump("normalise_denormal: out", vd);
}

u32 vfp_double_normaliseround(int dd, struct vfp_double *vd, u32 fpscr, u32 exceptions, const char *func)
{
	u64 significand, incr;
	int exponent, shift, underflow;
	u32 rmode;

	vfp_double_dump("pack: in", vd);

	/*
	 * Infinities and NaNs are a special case.
	 */
	if (vd->exponent == 2047 && (vd->significand == 0 || exceptions))
		goto pack;

	/*
	 * Special-case zero.
	 */
	if (vd->significand == 0) {
		vd->exponent = 0;
		goto pack;
	}

	exponent = vd->exponent;
	significand = vd->significand;

	shift = 32 - fls(significand >> 32);
	if (shift == 32)
		shift = 64 - fls(significand);
	if (shift) {
		exponent -= shift;
		significand <<= shift;
	}

#ifdef DEBUG
	vd->exponent = exponent;
	vd->significand = significand;
	vfp_double_dump("pack: normalised", vd);
#endif

	/*
	 * Tiny number?
	 */
	underflow = exponent < 0;
	if (underflow) {
		significand = vfp_shiftright64jamming(significand, -exponent);
		exponent = 0;
#ifdef DEBUG
		vd->exponent = exponent;
		vd->significand = significand;
		vfp_double_dump("pack: tiny number", vd);
#endif
		if (!(significand & ((1ULL << (VFP_DOUBLE_LOW_BITS + 1)) - 1)))
			underflow = 0;
	}

	/*
	 * Select rounding increment.
	 */
	incr = 0;
	rmode = fpscr & FPSCR_RMODE_MASK;

	if (rmode == FPSCR_ROUND_NEAREST) {
		incr = 1ULL << VFP_DOUBLE_LOW_BITS;
		if ((significand & (1ULL << (VFP_DOUBLE_LOW_BITS + 1))) == 0)
			incr -= 1;
	} else if (rmode == FPSCR_ROUND_TOZERO) {
		incr = 0;
	} else if ((rmode == FPSCR_ROUND_PLUSINF) ^ (vd->sign != 0))
		incr = (1ULL << (VFP_DOUBLE_LOW_BITS + 1)) - 1;

	pr_debug("VFP: rounding increment = 0x%08llx\n", incr);

	/*
	 * Is our rounding going to overflow?
	 */
	if ((significand + incr) < significand) {
		exponent += 1;
		significand = (significand >> 1) | (significand & 1);
		incr >>= 1;
#ifdef DEBUG
		vd->exponent = exponent;
		vd->significand = significand;
		vfp_double_dump("pack: overflow", vd);
#endif
	}

	/*
	 * If any of the low bits (which will be shifted out of the
	 * number) are non-zero, the result is inexact.
	 */
	if (significand & ((1 << (VFP_DOUBLE_LOW_BITS + 1)) - 1))
		exceptions |= FPSCR_IXC;

	/*
	 * Do our rounding.
	 */
	significand += incr;

	/*
	 * Infinity?
	 */
	if (exponent >= 2046) {
		exceptions |= FPSCR_OFC | FPSCR_IXC;
		if (incr == 0) {
			vd->exponent = 2045;
			vd->significand = 0x7fffffffffffffffULL;
		} else {
			vd->exponent = 2047;		/* infinity */
			vd->significand = 0;
		}
	} else {
		if (significand >> (VFP_DOUBLE_LOW_BITS + 1) == 0)
			exponent = 0;
		if (exponent || significand > 0x8000000000000000ULL)
			underflow = 0;
		if (underflow)
			exceptions |= FPSCR_UFC;
		vd->exponent = exponent;
		vd->significand = significand >> 1;
	}

 pack:
	vfp_double_dump("pack: final", vd);
	{
		s64 d = vfp_double_pack(vd);
		pr_debug("VFP: %s: d(d%d)=%016llx exceptions=%08x\n", func,
			 dd, d, exceptions);
		vfp_put_double(d, dd);
	}
	return exceptions;
}

/*
 * Propagate the NaN, setting exceptions if it is signalling.
 * 'n' is always a NaN.  'm' may be a number, NaN or infinity.
 */
static u32
vfp_propagate_nan(struct vfp_double *vdd, struct vfp_double *vdn,
		  struct vfp_double *vdm, u32 fpscr)
{
	struct vfp_double *nan;
	int tn, tm = 0;

	tn = vfp_double_type(vdn);

	if (vdm)
		tm = vfp_double_type(vdm);

	if (fpscr & FPSCR_DEFAULT_NAN)
		/*
		 * Default NaN mode - always returns a quiet NaN
		 */
		nan = &vfp_double_default_qnan;
	else {
		/*
		 * Contemporary mode - select the first signalling
		 * NAN, or if neither are signalling, the first
		 * quiet NAN.
		 */
		if (tn == VFP_SNAN || (tm != VFP_SNAN && tn == VFP_QNAN))
			nan = vdn;
		else
			nan = vdm;
		/*
		 * Make the NaN quiet.
		 */
		nan->significand |= VFP_DOUBLE_SIGNIFICAND_QNAN;
	}

	*vdd = *nan;

	/*
	 * If one was a signalling NAN, raise invalid operation.
	 */
	return tn == VFP_SNAN || tm == VFP_SNAN ? FPSCR_IOC : VFP_NAN_FLAG;
}

/*
 * Extended operations
 */
static u32 vfp_double_fabs(int dd, int unused, int dm, u32 fpscr)
{
	vfp_put_double(vfp_double_packed_abs(vfp_get_double(dm)), dd);
	return 0;
}

static u32 vfp_double_fcpy(int dd, int unused, int dm, u32 fpscr)
{
	vfp_put_double(vfp_get_double(dm), dd);
	return 0;
}

static u32 vfp_double_fneg(int dd, int unused, int dm, u32 fpscr)
{
	vfp_put_double(vfp_double_packed_negate(vfp_get_double(dm)), dd);
	return 0;
}

static u32 vfp_double_fsqrt(int dd, int unused, int dm, u32 fpscr)
{
	struct vfp_double vdm, vdd;
	int ret, tm;

	vfp_double_unpack(&vdm, vfp_get_double(dm));
	tm = vfp_double_type(&vdm);
	if (tm & (VFP_NAN|VFP_INFINITY)) {
		struct vfp_double *vdp = &vdd;

		if (tm & VFP_NAN)
			ret = vfp_propagate_nan(vdp, &vdm, NULL, fpscr);
		else if (vdm.sign == 0) {
 sqrt_copy:
			vdp = &vdm;
			ret = 0;
		} else {
 sqrt_invalid:
			vdp = &vfp_double_default_qnan;
			ret = FPSCR_IOC;
		}
		vfp_put_double(vfp_double_pack(vdp), dd);
		return ret;
	}

	/*
	 * sqrt(+/- 0) == +/- 0
	 */
	if (tm & VFP_ZERO)
		goto sqrt_copy;

	/*
	 * Normalise a denormalised number
	 */
	if (tm & VFP_DENORMAL)
		vfp_double_normalise_denormal(&vdm);

	/*
	 * sqrt(<0) = invalid
	 */
	if (vdm.sign)
		goto sqrt_invalid;

	vfp_double_dump("sqrt", &vdm);

	/*
	 * Estimate the square root.
	 */
	vdd.sign = 0;
	vdd.exponent = ((vdm.exponent - 1023) >> 1) + 1023;
	vdd.significand = (u64)vfp_estimate_sqrt_significand(vdm.exponent, vdm.significand >> 32) << 31;

	vfp_double_dump("sqrt estimate1", &vdd);

	vdm.significand >>= 1 + (vdm.exponent & 1);
	vdd.significand += 2 + vfp_estimate_div128to64(vdm.significand, 0, vdd.significand);

	vfp_double_dump("sqrt estimate2", &vdd);

	/*
	 * And now adjust.
	 */
	if ((vdd.significand & VFP_DOUBLE_LOW_BITS_MASK) <= 5) {
		if (vdd.significand < 2) {
			vdd.significand = ~0ULL;
		} else {
			u64 termh, terml, remh, reml;
			vdm.significand <<= 2;
			mul64to128(&termh, &terml, vdd.significand, vdd.significand);
			sub128(&remh, &reml, vdm.significand, 0, termh, terml);
			while ((s64)remh < 0) {
				vdd.significand -= 1;
				shift64left(&termh, &terml, vdd.significand);
				terml |= 1;
				add128(&remh, &reml, remh, reml, termh, terml);
			}
			vdd.significand |= (remh | reml) != 0;
		}
	}
	vdd.significand = vfp_shiftright64jamming(vdd.significand, 1);

	return vfp_double_normaliseround(dd, &vdd, fpscr, 0, "fsqrt");
}

/*
 * Equal	:= ZC
 * Less than	:= N
 * Greater than	:= C
 * Unordered	:= CV
 */
static u32 vfp_compare(int dd, int signal_on_qnan, int dm, u32 fpscr)
{
	s64 d, m;
	u32 ret = 0;

	m = vfp_get_double(dm);
	if (vfp_double_packed_exponent(m) == 2047 && vfp_double_packed_mantissa(m)) {
		ret |= FPSCR_C | FPSCR_V;
		if (signal_on_qnan || !(vfp_double_packed_mantissa(m) & (1ULL << (VFP_DOUBLE_MANTISSA_BITS - 1))))
			/*
			 * Signalling NaN, or signalling on quiet NaN
			 */
			ret |= FPSCR_IOC;
	}

	d = vfp_get_double(dd);
	if (vfp_double_packed_exponent(d) == 2047 && vfp_double_packed_mantissa(d)) {
		ret |= FPSCR_C | FPSCR_V;
		if (signal_on_qnan || !(vfp_double_packed_mantissa(d) & (1ULL << (VFP_DOUBLE_MANTISSA_BITS - 1))))
			/*
			 * Signalling NaN, or signalling on quiet NaN
			 */
			ret |= FPSCR_IOC;
	}

	if (ret == 0) {
		if (d == m || vfp_double_packed_abs(d | m) == 0) {
			/*
			 * equal
			 */
			ret |= FPSCR_Z | FPSCR_C;
		} else if (vfp_double_packed_sign(d ^ m)) {
			/*
			 * different signs
			 */
			if (vfp_double_packed_sign(d))
				/*
				 * d is negative, so d < m
				 */
				ret |= FPSCR_N;
			else
				/*
				 * d is positive, so d > m
				 */
				ret |= FPSCR_C;
		} else if ((vfp_double_packed_sign(d) != 0) ^ (d < m)) {
			/*
			 * d < m
			 */
			ret |= FPSCR_N;
		} else if ((vfp_double_packed_sign(d) != 0) ^ (d > m)) {
			/*
			 * d > m
			 */
			ret |= FPSCR_C;
		}
	}

	return ret;
}

static u32 vfp_double_fcmp(int dd, int unused, int dm, u32 fpscr)
{
	return vfp_compare(dd, 0, dm, fpscr);
}

static u32 vfp_double_fcmpe(int dd, int unused, int dm, u32 fpscr)
{
	return vfp_compare(dd, 1, dm, fpscr);
}

static u32 vfp_double_fcmpz(int dd, int unused, int dm, u32 fpscr)
{
	return vfp_compare(dd, 0, VFP_REG_ZERO, fpscr);
}

static u32 vfp_double_fcmpez(int dd, int unused, int dm, u32 fpscr)
{
	return vfp_compare(dd, 1, VFP_REG_ZERO, fpscr);
}

static u32 vfp_double_fcvts(int sd, int unused, int dm, u32 fpscr)
{
	struct vfp_double vdm;
	struct vfp_single vsd;
	int tm;
	u32 exceptions = 0;

	vfp_double_unpack(&vdm, vfp_get_double(dm));

	tm = vfp_double_type(&vdm);

	/*
	 * If we have a signalling NaN, signal invalid operation.
	 */
	if (tm == VFP_SNAN)
		exceptions = FPSCR_IOC;

	if (tm & VFP_DENORMAL)
		vfp_double_normalise_denormal(&vdm);

	vsd.sign = vdm.sign;
	vsd.significand = vfp_hi64to32jamming(vdm.significand);

	/*
	 * If we have an infinity or a NaN, the exponent must be 255
	 */
	if (tm & (VFP_INFINITY|VFP_NAN)) {
		vsd.exponent = 255;
		if (tm == VFP_QNAN)
			vsd.significand |= VFP_SINGLE_SIGNIFICAND_QNAN;
		goto pack_nan;
	} else if (tm & VFP_ZERO)
		vsd.exponent = 0;
	else
		vsd.exponent = vdm.exponent - (1023 - 127);

	return vfp_single_normaliseround(sd, &vsd, fpscr, exceptions, "fcvts");

 pack_nan:
	vfp_put_float(vfp_single_pack(&vsd), sd);
	return exceptions;
}

static u32 vfp_double_fuito(int dd, int unused, int dm, u32 fpscr)
{
	struct vfp_double vdm;
	u32 m = vfp_get_float(dm);

	vdm.sign = 0;
	vdm.exponent = 1023 + 63 - 1;
	vdm.significand = (u64)m;

	return vfp_double_normaliseround(dd, &vdm, fpscr, 0, "fuito");
}

static u32 vfp_double_fsito(int dd, int unused, int dm, u32 fpscr)
{
	struct vfp_double vdm;
	u32 m = vfp_get_float(dm);

	vdm.sign = (m & 0x80000000) >> 16;
	vdm.exponent = 1023 + 63 - 1;
	vdm.significand = vdm.sign ? -m : m;

	return vfp_double_normaliseround(dd, &vdm, fpscr, 0, "fsito");
}

static u32 vfp_double_ftoui(int sd, int unused, int dm, u32 fpscr)
{
	struct vfp_double vdm;
	u32 d, exceptions = 0;
	int rmode = fpscr & FPSCR_RMODE_MASK;
	int tm;

	vfp_double_unpack(&vdm, vfp_get_double(dm));

	/*
	 * Do we have a denormalised number?
	 */
	tm = vfp_double_type(&vdm);
	if (tm & VFP_DENORMAL)
		exceptions |= FPSCR_IDC;

	if (tm & VFP_NAN)
		vdm.sign = 0;

	if (vdm.exponent >= 1023 + 32) {
		d = vdm.sign ? 0 : 0xffffffff;
		exceptions = FPSCR_IOC;
	} else if (vdm.exponent >= 1023 - 1) {
		int shift = 1023 + 63 - vdm.exponent;
		u64 rem, incr = 0;

		/*
		 * 2^0 <= m < 2^32-2^8
		 */
		d = (vdm.significand << 1) >> shift;
		rem = vdm.significand << (65 - shift);

		if (rmode == FPSCR_ROUND_NEAREST) {
			incr = 0x8000000000000000ULL;
			if ((d & 1) == 0)
				incr -= 1;
		} else if (rmode == FPSCR_ROUND_TOZERO) {
			incr = 0;
		} else if ((rmode == FPSCR_ROUND_PLUSINF) ^ (vdm.sign != 0)) {
			incr = ~0ULL;
		}

		if ((rem + incr) < rem) {
			if (d < 0xffffffff)
				d += 1;
			else
				exceptions |= FPSCR_IOC;
		}

		if (d && vdm.sign) {
			d = 0;
			exceptions |= FPSCR_IOC;
		} else if (rem)
			exceptions |= FPSCR_IXC;
	} else {
		d = 0;
		if (vdm.exponent | vdm.significand) {
			exceptions |= FPSCR_IXC;
			if (rmode == FPSCR_ROUND_PLUSINF && vdm.sign == 0)
				d = 1;
			else if (rmode == FPSCR_ROUND_MINUSINF && vdm.sign) {
				d = 0;
				exceptions |= FPSCR_IOC;
			}
		}
	}

	pr_debug("VFP: ftoui: d(s%d)=%08x exceptions=%08x\n", sd, d, exceptions);

	vfp_put_float(d, sd);

	return exceptions;
}

static u32 vfp_double_ftouiz(int sd, int unused, int dm, u32 fpscr)
{
	return vfp_double_ftoui(sd, unused, dm, FPSCR_ROUND_TOZERO);
}

static u32 vfp_double_ftosi(int sd, int unused, int dm, u32 fpscr)
{
	struct vfp_double vdm;
	u32 d, exceptions = 0;
	int rmode = fpscr & FPSCR_RMODE_MASK;
	int tm;

	vfp_double_unpack(&vdm, vfp_get_double(dm));
	vfp_double_dump("VDM", &vdm);

	/*
	 * Do we have denormalised number?
	 */
	tm = vfp_double_type(&vdm);
	if (tm & VFP_DENORMAL)
		exceptions |= FPSCR_IDC;

	if (tm & VFP_NAN) {
		d = 0;
		exceptions |= FPSCR_IOC;
	} else if (vdm.exponent >= 1023 + 32) {
		d = 0x7fffffff;
		if (vdm.sign)
			d = ~d;
		exceptions |= FPSCR_IOC;
	} else if (vdm.exponent >= 1023 - 1) {
		int shift = 1023 + 63 - vdm.exponent;	/* 58 */
		u64 rem, incr = 0;

		d = (vdm.significand << 1) >> shift;
		rem = vdm.significand << (65 - shift);

		if (rmode == FPSCR_ROUND_NEAREST) {
			incr = 0x8000000000000000ULL;
			if ((d & 1) == 0)
				incr -= 1;
		} else if (rmode == FPSCR_ROUND_TOZERO) {
			incr = 0;
		} else if ((rmode == FPSCR_ROUND_PLUSINF) ^ (vdm.sign != 0)) {
			incr = ~0ULL;
		}

		if ((rem + incr) < rem && d < 0xffffffff)
			d += 1;
		if (d > 0x7fffffff + (vdm.sign != 0)) {
			d = 0x7fffffff + (vdm.sign != 0);
			exceptions |= FPSCR_IOC;
		} else if (rem)
			exceptions |= FPSCR_IXC;

		if (vdm.sign)
			d = -d;
	} else {
		d = 0;
		if (vdm.exponent | vdm.significand) {
			exceptions |= FPSCR_IXC;
			if (rmode == FPSCR_ROUND_PLUSINF && vdm.sign == 0)
				d = 1;
			else if (rmode == FPSCR_ROUND_MINUSINF && vdm.sign)
				d = -1;
		}
	}

	pr_debug("VFP: ftosi: d(s%d)=%08x exceptions=%08x\n", sd, d, exceptions);

	vfp_put_float((s32)d, sd);

	return exceptions;
}

static u32 vfp_double_ftosiz(int dd, int unused, int dm, u32 fpscr)
{
	return vfp_double_ftosi(dd, unused, dm, FPSCR_ROUND_TOZERO);
}


static struct op fops_ext[32] = {
	[FEXT_TO_IDX(FEXT_FCPY)]	= { vfp_double_fcpy,   0 },
	[FEXT_TO_IDX(FEXT_FABS)]	= { vfp_double_fabs,   0 },
	[FEXT_TO_IDX(FEXT_FNEG)]	= { vfp_double_fneg,   0 },
	[FEXT_TO_IDX(FEXT_FSQRT)]	= { vfp_double_fsqrt,  0 },
	[FEXT_TO_IDX(FEXT_FCMP)]	= { vfp_double_fcmp,   OP_SCALAR },
	[FEXT_TO_IDX(FEXT_FCMPE)]	= { vfp_double_fcmpe,  OP_SCALAR },
	[FEXT_TO_IDX(FEXT_FCMPZ)]	= { vfp_double_fcmpz,  OP_SCALAR },
	[FEXT_TO_IDX(FEXT_FCMPEZ)]	= { vfp_double_fcmpez, OP_SCALAR },
	[FEXT_TO_IDX(FEXT_FCVT)]	= { vfp_double_fcvts,  OP_SCALAR|OP_SD },
	[FEXT_TO_IDX(FEXT_FUITO)]	= { vfp_double_fuito,  OP_SCALAR|OP_SM },
	[FEXT_TO_IDX(FEXT_FSITO)]	= { vfp_double_fsito,  OP_SCALAR|OP_SM },
	[FEXT_TO_IDX(FEXT_FTOUI)]	= { vfp_double_ftoui,  OP_SCALAR|OP_SD },
	[FEXT_TO_IDX(FEXT_FTOUIZ)]	= { vfp_double_ftouiz, OP_SCALAR|OP_SD },
	[FEXT_TO_IDX(FEXT_FTOSI)]	= { vfp_double_ftosi,  OP_SCALAR|OP_SD },
	[FEXT_TO_IDX(FEXT_FTOSIZ)]	= { vfp_double_ftosiz, OP_SCALAR|OP_SD },
};




static u32
vfp_double_fadd_nonnumber(struct vfp_double *vdd, struct vfp_double *vdn,
			  struct vfp_double *vdm, u32 fpscr)
{
	struct vfp_double *vdp;
	u32 exceptions = 0;
	int tn, tm;

	tn = vfp_double_type(vdn);
	tm = vfp_double_type(vdm);

	if (tn & tm & VFP_INFINITY) {
		/*
		 * Two infinities.  Are they different signs?
		 */
		if (vdn->sign ^ vdm->sign) {
			/*
			 * different signs -> invalid
			 */
			exceptions = FPSCR_IOC;
			vdp = &vfp_double_default_qnan;
		} else {
			/*
			 * same signs -> valid
			 */
			vdp = vdn;
		}
	} else if (tn & VFP_INFINITY && tm & VFP_NUMBER) {
		/*
		 * One infinity and one number -> infinity
		 */
		vdp = vdn;
	} else {
		/*
		 * 'n' is a NaN of some type
		 */
		return vfp_propagate_nan(vdd, vdn, vdm, fpscr);
	}
	*vdd = *vdp;
	return exceptions;
}

static u32
vfp_double_add(struct vfp_double *vdd, struct vfp_double *vdn,
	       struct vfp_double *vdm, u32 fpscr)
{
	u32 exp_diff;
	u64 m_sig;

	if (vdn->significand & (1ULL << 63) ||
	    vdm->significand & (1ULL << 63)) {
		pr_info("VFP: bad FP values in %s\n", __func__);
		vfp_double_dump("VDN", vdn);
		vfp_double_dump("VDM", vdm);
	}

	/*
	 * Ensure that 'n' is the largest magnitude number.  Note that
	 * if 'n' and 'm' have equal exponents, we do not swap them.
	 * This ensures that NaN propagation works correctly.
	 */
	if (vdn->exponent < vdm->exponent) {
		struct vfp_double *t = vdn;
		vdn = vdm;
		vdm = t;
	}

	/*
	 * Is 'n' an infinity or a NaN?  Note that 'm' may be a number,
	 * infinity or a NaN here.
	 */
	if (vdn->exponent == 2047)
		return vfp_double_fadd_nonnumber(vdd, vdn, vdm, fpscr);

	/*
	 * We have two proper numbers, where 'vdn' is the larger magnitude.
	 *
	 * Copy 'n' to 'd' before doing the arithmetic.
	 */
	*vdd = *vdn;

	/*
	 * Align 'm' with the result.
	 */
	exp_diff = vdn->exponent - vdm->exponent;
	m_sig = vfp_shiftright64jamming(vdm->significand, exp_diff);

	/*
	 * If the signs are different, we are really subtracting.
	 */
	if (vdn->sign ^ vdm->sign) {
		m_sig = vdn->significand - m_sig;
		if ((s64)m_sig < 0) {
			vdd->sign = vfp_sign_negate(vdd->sign);
			m_sig = -m_sig;
		} else if (m_sig == 0) {
			vdd->sign = (fpscr & FPSCR_RMODE_MASK) ==
				      FPSCR_ROUND_MINUSINF ? 0x8000 : 0;
		}
	} else {
		m_sig += vdn->significand;
	}
	vdd->significand = m_sig;

	return 0;
}

static u32
vfp_double_multiply(struct vfp_double *vdd, struct vfp_double *vdn,
		    struct vfp_double *vdm, u32 fpscr)
{
	vfp_double_dump("VDN", vdn);
	vfp_double_dump("VDM", vdm);

	/*
	 * Ensure that 'n' is the largest magnitude number.  Note that
	 * if 'n' and 'm' have equal exponents, we do not swap them.
	 * This ensures that NaN propagation works correctly.
	 */
	if (vdn->exponent < vdm->exponent) {
		struct vfp_double *t = vdn;
		vdn = vdm;
		vdm = t;
		pr_debug("VFP: swapping M <-> N\n");
	}

	vdd->sign = vdn->sign ^ vdm->sign;

	/*
	 * If 'n' is an infinity or NaN, handle it.  'm' may be anything.
	 */
	if (vdn->exponent == 2047) {
		if (vdn->significand || (vdm->exponent == 2047 && vdm->significand))
			return vfp_propagate_nan(vdd, vdn, vdm, fpscr);
		if ((vdm->exponent | vdm->significand) == 0) {
			*vdd = vfp_double_default_qnan;
			return FPSCR_IOC;
		}
		vdd->exponent = vdn->exponent;
		vdd->significand = 0;
		return 0;
	}

	/*
	 * If 'm' is zero, the result is always zero.  In this case,
	 * 'n' may be zero or a number, but it doesn't matter which.
	 */
	if ((vdm->exponent | vdm->significand) == 0) {
		vdd->exponent = 0;
		vdd->significand = 0;
		return 0;
	}

	/*
	 * We add 2 to the destination exponent for the same reason
	 * as the addition case - though this time we have +1 from
	 * each input operand.
	 */
	vdd->exponent = vdn->exponent + vdm->exponent - 1023 + 2;
	vdd->significand = vfp_hi64multiply64(vdn->significand, vdm->significand);

	vfp_double_dump("VDD", vdd);
	return 0;
}

#define NEG_MULTIPLY	(1 << 0)
#define NEG_SUBTRACT	(1 << 1)

static u32
vfp_double_multiply_accumulate(int dd, int dn, int dm, u32 fpscr, u32 negate, char *func)
{
	struct vfp_double vdd, vdp, vdn, vdm;
	u32 exceptions;

	vfp_double_unpack(&vdn, vfp_get_double(dn));
	if (vdn.exponent == 0 && vdn.significand)
		vfp_double_normalise_denormal(&vdn);

	vfp_double_unpack(&vdm, vfp_get_double(dm));
	if (vdm.exponent == 0 && vdm.significand)
		vfp_double_normalise_denormal(&vdm);

	exceptions = vfp_double_multiply(&vdp, &vdn, &vdm, fpscr);
	if (negate & NEG_MULTIPLY)
		vdp.sign = vfp_sign_negate(vdp.sign);

	vfp_double_unpack(&vdn, vfp_get_double(dd));
	if (negate & NEG_SUBTRACT)
		vdn.sign = vfp_sign_negate(vdn.sign);

	exceptions |= vfp_double_add(&vdd, &vdn, &vdp, fpscr);

	return vfp_double_normaliseround(dd, &vdd, fpscr, exceptions, func);
}

/*
 * Standard operations
 */

/*
 * sd = sd + (sn * sm)
 */
static u32 vfp_double_fmac(int dd, int dn, int dm, u32 fpscr)
{
	return vfp_double_multiply_accumulate(dd, dn, dm, fpscr, 0, "fmac");
}

/*
 * sd = sd - (sn * sm)
 */
static u32 vfp_double_fnmac(int dd, int dn, int dm, u32 fpscr)
{
	return vfp_double_multiply_accumulate(dd, dn, dm, fpscr, NEG_MULTIPLY, "fnmac");
}

/*
 * sd = -sd + (sn * sm)
 */
static u32 vfp_double_fmsc(int dd, int dn, int dm, u32 fpscr)
{
	return vfp_double_multiply_accumulate(dd, dn, dm, fpscr, NEG_SUBTRACT, "fmsc");
}

/*
 * sd = -sd - (sn * sm)
 */
static u32 vfp_double_fnmsc(int dd, int dn, int dm, u32 fpscr)
{
	return vfp_double_multiply_accumulate(dd, dn, dm, fpscr, NEG_SUBTRACT | NEG_MULTIPLY, "fnmsc");
}

/*
 * sd = sn * sm
 */
static u32 vfp_double_fmul(int dd, int dn, int dm, u32 fpscr)
{
	struct vfp_double vdd, vdn, vdm;
	u32 exceptions;

	vfp_double_unpack(&vdn, vfp_get_double(dn));
	if (vdn.exponent == 0 && vdn.significand)
		vfp_double_normalise_denormal(&vdn);

	vfp_double_unpack(&vdm, vfp_get_double(dm));
	if (vdm.exponent == 0 && vdm.significand)
		vfp_double_normalise_denormal(&vdm);

	exceptions = vfp_double_multiply(&vdd, &vdn, &vdm, fpscr);
	return vfp_double_normaliseround(dd, &vdd, fpscr, exceptions, "fmul");
}

/*
 * sd = -(sn * sm)
 */
static u32 vfp_double_fnmul(int dd, int dn, int dm, u32 fpscr)
{
	struct vfp_double vdd, vdn, vdm;
	u32 exceptions;

	vfp_double_unpack(&vdn, vfp_get_double(dn));
	if (vdn.exponent == 0 && vdn.significand)
		vfp_double_normalise_denormal(&vdn);

	vfp_double_unpack(&vdm, vfp_get_double(dm));
	if (vdm.exponent == 0 && vdm.significand)
		vfp_double_normalise_denormal(&vdm);

	exceptions = vfp_double_multiply(&vdd, &vdn, &vdm, fpscr);
	vdd.sign = vfp_sign_negate(vdd.sign);

	return vfp_double_normaliseround(dd, &vdd, fpscr, exceptions, "fnmul");
}

/*
 * sd = sn + sm
 */
static u32 vfp_double_fadd(int dd, int dn, int dm, u32 fpscr)
{
	struct vfp_double vdd, vdn, vdm;
	u32 exceptions;

	vfp_double_unpack(&vdn, vfp_get_double(dn));
	if (vdn.exponent == 0 && vdn.significand)
		vfp_double_normalise_denormal(&vdn);

	vfp_double_unpack(&vdm, vfp_get_double(dm));
	if (vdm.exponent == 0 && vdm.significand)
		vfp_double_normalise_denormal(&vdm);

	exceptions = vfp_double_add(&vdd, &vdn, &vdm, fpscr);

	return vfp_double_normaliseround(dd, &vdd, fpscr, exceptions, "fadd");
}

/*
 * sd = sn - sm
 */
static u32 vfp_double_fsub(int dd, int dn, int dm, u32 fpscr)
{
	struct vfp_double vdd, vdn, vdm;
	u32 exceptions;

	vfp_double_unpack(&vdn, vfp_get_double(dn));
	if (vdn.exponent == 0 && vdn.significand)
		vfp_double_normalise_denormal(&vdn);

	vfp_double_unpack(&vdm, vfp_get_double(dm));
	if (vdm.exponent == 0 && vdm.significand)
		vfp_double_normalise_denormal(&vdm);

	/*
	 * Subtraction is like addition, but with a negated operand.
	 */
	vdm.sign = vfp_sign_negate(vdm.sign);

	exceptions = vfp_double_add(&vdd, &vdn, &vdm, fpscr);

	return vfp_double_normaliseround(dd, &vdd, fpscr, exceptions, "fsub");
}

/*
 * sd = sn / sm
 */
static u32 vfp_double_fdiv(int dd, int dn, int dm, u32 fpscr)
{
	struct vfp_double vdd, vdn, vdm;
	u32 exceptions = 0;
	int tm, tn;

	vfp_double_unpack(&vdn, vfp_get_double(dn));
	vfp_double_unpack(&vdm, vfp_get_double(dm));

	vdd.sign = vdn.sign ^ vdm.sign;

	tn = vfp_double_type(&vdn);
	tm = vfp_double_type(&vdm);

	/*
	 * Is n a NAN?
	 */
	if (tn & VFP_NAN)
		goto vdn_nan;

	/*
	 * Is m a NAN?
	 */
	if (tm & VFP_NAN)
		goto vdm_nan;

	/*
	 * If n and m are infinity, the result is invalid
	 * If n and m are zero, the result is invalid
	 */
	if (tm & tn & (VFP_INFINITY|VFP_ZERO))
		goto invalid;

	/*
	 * If n is infinity, the result is infinity
	 */
	if (tn & VFP_INFINITY)
		goto infinity;

	/*
	 * If m is zero, raise div0 exceptions
	 */
	if (tm & VFP_ZERO)
		goto divzero;

	/*
	 * If m is infinity, or n is zero, the result is zero
	 */
	if (tm & VFP_INFINITY || tn & VFP_ZERO)
		goto zero;

	if (tn & VFP_DENORMAL)
		vfp_double_normalise_denormal(&vdn);
	if (tm & VFP_DENORMAL)
		vfp_double_normalise_denormal(&vdm);

	/*
	 * Ok, we have two numbers, we can perform division.
	 */
	vdd.exponent = vdn.exponent - vdm.exponent + 1023 - 1;
	vdm.significand <<= 1;
	if (vdm.significand <= (2 * vdn.significand)) {
		vdn.significand >>= 1;
		vdd.exponent++;
	}
	vdd.significand = vfp_estimate_div128to64(vdn.significand, 0, vdm.significand);
	if ((vdd.significand & 0x1ff) <= 2) {
		u64 termh, terml, remh, reml;
		mul64to128(&termh, &terml, vdm.significand, vdd.significand);
		sub128(&remh, &reml, vdn.significand, 0, termh, terml);
		while ((s64)remh < 0) {
			vdd.significand -= 1;
			add128(&remh, &reml, remh, reml, 0, vdm.significand);
		}
		vdd.significand |= (reml != 0);
	}
	return vfp_double_normaliseround(dd, &vdd, fpscr, 0, "fdiv");

 vdn_nan:
	exceptions = vfp_propagate_nan(&vdd, &vdn, &vdm, fpscr);
 pack:
	vfp_put_double(vfp_double_pack(&vdd), dd);
	return exceptions;

 vdm_nan:
	exceptions = vfp_propagate_nan(&vdd, &vdm, &vdn, fpscr);
	goto pack;

 zero:
	vdd.exponent = 0;
	vdd.significand = 0;
	goto pack;

 divzero:
	exceptions = FPSCR_DZC;
 infinity:
	vdd.exponent = 2047;
	vdd.significand = 0;
	goto pack;

 invalid:
	vfp_put_double(vfp_double_pack(&vfp_double_default_qnan), dd);
	return FPSCR_IOC;
}

static struct op fops[16] = {
	[FOP_TO_IDX(FOP_FMAC)]	= { vfp_double_fmac,  0 },
	[FOP_TO_IDX(FOP_FNMAC)]	= { vfp_double_fnmac, 0 },
	[FOP_TO_IDX(FOP_FMSC)]	= { vfp_double_fmsc,  0 },
	[FOP_TO_IDX(FOP_FNMSC)]	= { vfp_double_fnmsc, 0 },
	[FOP_TO_IDX(FOP_FMUL)]	= { vfp_double_fmul,  0 },
	[FOP_TO_IDX(FOP_FNMUL)]	= { vfp_double_fnmul, 0 },
	[FOP_TO_IDX(FOP_FADD)]	= { vfp_double_fadd,  0 },
	[FOP_TO_IDX(FOP_FSUB)]	= { vfp_double_fsub,  0 },
	[FOP_TO_IDX(FOP_FDIV)]	= { vfp_double_fdiv,  0 },
};

#define FREG_BANK(x)	((x) & 0x0c)
#define FREG_IDX(x)	((x) & 3)

u32 vfp_double_cpdo(u32 inst, u32 fpscr)
{
	u32 op = inst & FOP_MASK;
	u32 exceptions = 0;
	unsigned int dest;
	unsigned int dn = vfp_get_dn(inst);
	unsigned int dm;
	unsigned int vecitr, veclen, vecstride;
	struct op *fop;

	vecstride = (1 + ((fpscr & FPSCR_STRIDE_MASK) == FPSCR_STRIDE_MASK));

	fop = (op == FOP_EXT) ? &fops_ext[FEXT_TO_IDX(inst)] : &fops[FOP_TO_IDX(op)];

	/*
	 * fcvtds takes an sN register number as destination, not dN.
	 * It also always operates on scalars.
	 */
	if (fop->flags & OP_SD)
		dest = vfp_get_sd(inst);
	else
		dest = vfp_get_dd(inst);

	/*
	 * f[us]ito takes a sN operand, not a dN operand.
	 */
	if (fop->flags & OP_SM)
		dm = vfp_get_sm(inst);
	else
		dm = vfp_get_dm(inst);

	/*
	 * If destination bank is zero, vector length is always '1'.
	 * ARM DDI0100F C5.1.3, C5.3.2.
	 */
	if ((fop->flags & OP_SCALAR) || (FREG_BANK(dest) == 0))
		veclen = 0;
	else
		veclen = fpscr & FPSCR_LENGTH_MASK;

	pr_debug("VFP: vecstride=%u veclen=%u\n", vecstride,
		 (veclen >> FPSCR_LENGTH_BIT) + 1);

	if (!fop->fn)
		goto invalid;

	for (vecitr = 0; vecitr <= veclen; vecitr += 1 << FPSCR_LENGTH_BIT) {
		u32 except;
		char type;

		type = fop->flags & OP_SD ? 's' : 'd';
		if (op == FOP_EXT)
			pr_debug("VFP: itr%d (%c%u) = op[%u] (d%u)\n",
				 vecitr >> FPSCR_LENGTH_BIT,
				 type, dest, dn, dm);
		else
			pr_debug("VFP: itr%d (%c%u) = (d%u) op[%u] (d%u)\n",
				 vecitr >> FPSCR_LENGTH_BIT,
				 type, dest, dn, FOP_TO_IDX(op), dm);

		except = fop->fn(dest, dn, dm, fpscr);
		pr_debug("VFP: itr%d: exceptions=%08x\n",
			 vecitr >> FPSCR_LENGTH_BIT, except);

		exceptions |= except;

		/*
		 * CHECK: It appears to be undefined whether we stop when
		 * we encounter an exception.  We continue.
		 */
		dest = FREG_BANK(dest) + ((FREG_IDX(dest) + vecstride) & 3);
		dn = FREG_BANK(dn) + ((FREG_IDX(dn) + vecstride) & 3);
		if (FREG_BANK(dm) != 0)
			dm = FREG_BANK(dm) + ((FREG_IDX(dm) + vecstride) & 3);
	}
	return exceptions;

 invalid:
	return ~0;
}
