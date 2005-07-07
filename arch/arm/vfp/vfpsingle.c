/*
 *  linux/arch/arm/vfp/vfpsingle.c
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
#include <asm/ptrace.h>
#include <asm/vfp.h>

#include "vfpinstr.h"
#include "vfp.h"

static struct vfp_single vfp_single_default_qnan = {
	.exponent	= 255,
	.sign		= 0,
	.significand	= VFP_SINGLE_SIGNIFICAND_QNAN,
};

static void vfp_single_dump(const char *str, struct vfp_single *s)
{
	pr_debug("VFP: %s: sign=%d exponent=%d significand=%08x\n",
		 str, s->sign != 0, s->exponent, s->significand);
}

static void vfp_single_normalise_denormal(struct vfp_single *vs)
{
	int bits = 31 - fls(vs->significand);

	vfp_single_dump("normalise_denormal: in", vs);

	if (bits) {
		vs->exponent -= bits - 1;
		vs->significand <<= bits;
	}

	vfp_single_dump("normalise_denormal: out", vs);
}

#ifndef DEBUG
#define vfp_single_normaliseround(sd,vsd,fpscr,except,func) __vfp_single_normaliseround(sd,vsd,fpscr,except)
u32 __vfp_single_normaliseround(int sd, struct vfp_single *vs, u32 fpscr, u32 exceptions)
#else
u32 vfp_single_normaliseround(int sd, struct vfp_single *vs, u32 fpscr, u32 exceptions, const char *func)
#endif
{
	u32 significand, incr, rmode;
	int exponent, shift, underflow;

	vfp_single_dump("pack: in", vs);

	/*
	 * Infinities and NaNs are a special case.
	 */
	if (vs->exponent == 255 && (vs->significand == 0 || exceptions))
		goto pack;

	/*
	 * Special-case zero.
	 */
	if (vs->significand == 0) {
		vs->exponent = 0;
		goto pack;
	}

	exponent = vs->exponent;
	significand = vs->significand;

	/*
	 * Normalise first.  Note that we shift the significand up to
	 * bit 31, so we have VFP_SINGLE_LOW_BITS + 1 below the least
	 * significant bit.
	 */
	shift = 32 - fls(significand);
	if (shift < 32 && shift) {
		exponent -= shift;
		significand <<= shift;
	}

#ifdef DEBUG
	vs->exponent = exponent;
	vs->significand = significand;
	vfp_single_dump("pack: normalised", vs);
#endif

	/*
	 * Tiny number?
	 */
	underflow = exponent < 0;
	if (underflow) {
		significand = vfp_shiftright32jamming(significand, -exponent);
		exponent = 0;
#ifdef DEBUG
		vs->exponent = exponent;
		vs->significand = significand;
		vfp_single_dump("pack: tiny number", vs);
#endif
		if (!(significand & ((1 << (VFP_SINGLE_LOW_BITS + 1)) - 1)))
			underflow = 0;
	}

	/*
	 * Select rounding increment.
	 */
	incr = 0;
	rmode = fpscr & FPSCR_RMODE_MASK;

	if (rmode == FPSCR_ROUND_NEAREST) {
		incr = 1 << VFP_SINGLE_LOW_BITS;
		if ((significand & (1 << (VFP_SINGLE_LOW_BITS + 1))) == 0)
			incr -= 1;
	} else if (rmode == FPSCR_ROUND_TOZERO) {
		incr = 0;
	} else if ((rmode == FPSCR_ROUND_PLUSINF) ^ (vs->sign != 0))
		incr = (1 << (VFP_SINGLE_LOW_BITS + 1)) - 1;

	pr_debug("VFP: rounding increment = 0x%08x\n", incr);

	/*
	 * Is our rounding going to overflow?
	 */
	if ((significand + incr) < significand) {
		exponent += 1;
		significand = (significand >> 1) | (significand & 1);
		incr >>= 1;
#ifdef DEBUG
		vs->exponent = exponent;
		vs->significand = significand;
		vfp_single_dump("pack: overflow", vs);
#endif
	}

	/*
	 * If any of the low bits (which will be shifted out of the
	 * number) are non-zero, the result is inexact.
	 */
	if (significand & ((1 << (VFP_SINGLE_LOW_BITS + 1)) - 1))
		exceptions |= FPSCR_IXC;

	/*
	 * Do our rounding.
	 */
	significand += incr;

	/*
	 * Infinity?
	 */
	if (exponent >= 254) {
		exceptions |= FPSCR_OFC | FPSCR_IXC;
		if (incr == 0) {
			vs->exponent = 253;
			vs->significand = 0x7fffffff;
		} else {
			vs->exponent = 255;		/* infinity */
			vs->significand = 0;
		}
	} else {
		if (significand >> (VFP_SINGLE_LOW_BITS + 1) == 0)
			exponent = 0;
		if (exponent || significand > 0x80000000)
			underflow = 0;
		if (underflow)
			exceptions |= FPSCR_UFC;
		vs->exponent = exponent;
		vs->significand = significand >> 1;
	}

 pack:
	vfp_single_dump("pack: final", vs);
	{
		s32 d = vfp_single_pack(vs);
		pr_debug("VFP: %s: d(s%d)=%08x exceptions=%08x\n", func,
			 sd, d, exceptions);
		vfp_put_float(sd, d);
	}

	return exceptions & ~VFP_NAN_FLAG;
}

/*
 * Propagate the NaN, setting exceptions if it is signalling.
 * 'n' is always a NaN.  'm' may be a number, NaN or infinity.
 */
static u32
vfp_propagate_nan(struct vfp_single *vsd, struct vfp_single *vsn,
		  struct vfp_single *vsm, u32 fpscr)
{
	struct vfp_single *nan;
	int tn, tm = 0;

	tn = vfp_single_type(vsn);

	if (vsm)
		tm = vfp_single_type(vsm);

	if (fpscr & FPSCR_DEFAULT_NAN)
		/*
		 * Default NaN mode - always returns a quiet NaN
		 */
		nan = &vfp_single_default_qnan;
	else {
		/*
		 * Contemporary mode - select the first signalling
		 * NAN, or if neither are signalling, the first
		 * quiet NAN.
		 */
		if (tn == VFP_SNAN || (tm != VFP_SNAN && tn == VFP_QNAN))
			nan = vsn;
		else
			nan = vsm;
		/*
		 * Make the NaN quiet.
		 */
		nan->significand |= VFP_SINGLE_SIGNIFICAND_QNAN;
	}

	*vsd = *nan;

	/*
	 * If one was a signalling NAN, raise invalid operation.
	 */
	return tn == VFP_SNAN || tm == VFP_SNAN ? FPSCR_IOC : VFP_NAN_FLAG;
}


/*
 * Extended operations
 */
static u32 vfp_single_fabs(int sd, int unused, s32 m, u32 fpscr)
{
	vfp_put_float(sd, vfp_single_packed_abs(m));
	return 0;
}

static u32 vfp_single_fcpy(int sd, int unused, s32 m, u32 fpscr)
{
	vfp_put_float(sd, m);
	return 0;
}

static u32 vfp_single_fneg(int sd, int unused, s32 m, u32 fpscr)
{
	vfp_put_float(sd, vfp_single_packed_negate(m));
	return 0;
}

static const u16 sqrt_oddadjust[] = {
	0x0004, 0x0022, 0x005d, 0x00b1, 0x011d, 0x019f, 0x0236, 0x02e0,
	0x039c, 0x0468, 0x0545, 0x0631, 0x072b, 0x0832, 0x0946, 0x0a67
};

static const u16 sqrt_evenadjust[] = {
	0x0a2d, 0x08af, 0x075a, 0x0629, 0x051a, 0x0429, 0x0356, 0x029e,
	0x0200, 0x0179, 0x0109, 0x00af, 0x0068, 0x0034, 0x0012, 0x0002
};

u32 vfp_estimate_sqrt_significand(u32 exponent, u32 significand)
{
	int index;
	u32 z, a;

	if ((significand & 0xc0000000) != 0x40000000) {
		printk(KERN_WARNING "VFP: estimate_sqrt: invalid significand\n");
	}

	a = significand << 1;
	index = (a >> 27) & 15;
	if (exponent & 1) {
		z = 0x4000 + (a >> 17) - sqrt_oddadjust[index];
		z = ((a / z) << 14) + (z << 15);
		a >>= 1;
	} else {
		z = 0x8000 + (a >> 17) - sqrt_evenadjust[index];
		z = a / z + z;
		z = (z >= 0x20000) ? 0xffff8000 : (z << 15);
		if (z <= a)
			return (s32)a >> 1;
	}
	{
		u64 v = (u64)a << 31;
		do_div(v, z);
		return v + (z >> 1);
	}
}

static u32 vfp_single_fsqrt(int sd, int unused, s32 m, u32 fpscr)
{
	struct vfp_single vsm, vsd;
	int ret, tm;

	vfp_single_unpack(&vsm, m);
	tm = vfp_single_type(&vsm);
	if (tm & (VFP_NAN|VFP_INFINITY)) {
		struct vfp_single *vsp = &vsd;

		if (tm & VFP_NAN)
			ret = vfp_propagate_nan(vsp, &vsm, NULL, fpscr);
		else if (vsm.sign == 0) {
 sqrt_copy:
			vsp = &vsm;
			ret = 0;
		} else {
 sqrt_invalid:
			vsp = &vfp_single_default_qnan;
			ret = FPSCR_IOC;
		}
		vfp_put_float(sd, vfp_single_pack(vsp));
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
		vfp_single_normalise_denormal(&vsm);

	/*
	 * sqrt(<0) = invalid
	 */
	if (vsm.sign)
		goto sqrt_invalid;

	vfp_single_dump("sqrt", &vsm);

	/*
	 * Estimate the square root.
	 */
	vsd.sign = 0;
	vsd.exponent = ((vsm.exponent - 127) >> 1) + 127;
	vsd.significand = vfp_estimate_sqrt_significand(vsm.exponent, vsm.significand) + 2;

	vfp_single_dump("sqrt estimate", &vsd);

	/*
	 * And now adjust.
	 */
	if ((vsd.significand & VFP_SINGLE_LOW_BITS_MASK) <= 5) {
		if (vsd.significand < 2) {
			vsd.significand = 0xffffffff;
		} else {
			u64 term;
			s64 rem;
			vsm.significand <<= !(vsm.exponent & 1);
			term = (u64)vsd.significand * vsd.significand;
			rem = ((u64)vsm.significand << 32) - term;

			pr_debug("VFP: term=%016llx rem=%016llx\n", term, rem);

			while (rem < 0) {
				vsd.significand -= 1;
				rem += ((u64)vsd.significand << 1) | 1;
			}
			vsd.significand |= rem != 0;
		}
	}
	vsd.significand = vfp_shiftright32jamming(vsd.significand, 1);

	return vfp_single_normaliseround(sd, &vsd, fpscr, 0, "fsqrt");
}

/*
 * Equal	:= ZC
 * Less than	:= N
 * Greater than	:= C
 * Unordered	:= CV
 */
static u32 vfp_compare(int sd, int signal_on_qnan, s32 m, u32 fpscr)
{
	s32 d;
	u32 ret = 0;

	d = vfp_get_float(sd);
	if (vfp_single_packed_exponent(m) == 255 && vfp_single_packed_mantissa(m)) {
		ret |= FPSCR_C | FPSCR_V;
		if (signal_on_qnan || !(vfp_single_packed_mantissa(m) & (1 << (VFP_SINGLE_MANTISSA_BITS - 1))))
			/*
			 * Signalling NaN, or signalling on quiet NaN
			 */
			ret |= FPSCR_IOC;
	}

	if (vfp_single_packed_exponent(d) == 255 && vfp_single_packed_mantissa(d)) {
		ret |= FPSCR_C | FPSCR_V;
		if (signal_on_qnan || !(vfp_single_packed_mantissa(d) & (1 << (VFP_SINGLE_MANTISSA_BITS - 1))))
			/*
			 * Signalling NaN, or signalling on quiet NaN
			 */
			ret |= FPSCR_IOC;
	}

	if (ret == 0) {
		if (d == m || vfp_single_packed_abs(d | m) == 0) {
			/*
			 * equal
			 */
			ret |= FPSCR_Z | FPSCR_C;
		} else if (vfp_single_packed_sign(d ^ m)) {
			/*
			 * different signs
			 */
			if (vfp_single_packed_sign(d))
				/*
				 * d is negative, so d < m
				 */
				ret |= FPSCR_N;
			else
				/*
				 * d is positive, so d > m
				 */
				ret |= FPSCR_C;
		} else if ((vfp_single_packed_sign(d) != 0) ^ (d < m)) {
			/*
			 * d < m
			 */
			ret |= FPSCR_N;
		} else if ((vfp_single_packed_sign(d) != 0) ^ (d > m)) {
			/*
			 * d > m
			 */
			ret |= FPSCR_C;
		}
	}
	return ret;
}

static u32 vfp_single_fcmp(int sd, int unused, s32 m, u32 fpscr)
{
	return vfp_compare(sd, 0, m, fpscr);
}

static u32 vfp_single_fcmpe(int sd, int unused, s32 m, u32 fpscr)
{
	return vfp_compare(sd, 1, m, fpscr);
}

static u32 vfp_single_fcmpz(int sd, int unused, s32 m, u32 fpscr)
{
	return vfp_compare(sd, 0, 0, fpscr);
}

static u32 vfp_single_fcmpez(int sd, int unused, s32 m, u32 fpscr)
{
	return vfp_compare(sd, 1, 0, fpscr);
}

static u32 vfp_single_fcvtd(int dd, int unused, s32 m, u32 fpscr)
{
	struct vfp_single vsm;
	struct vfp_double vdd;
	int tm;
	u32 exceptions = 0;

	vfp_single_unpack(&vsm, m);

	tm = vfp_single_type(&vsm);

	/*
	 * If we have a signalling NaN, signal invalid operation.
	 */
	if (tm == VFP_SNAN)
		exceptions = FPSCR_IOC;

	if (tm & VFP_DENORMAL)
		vfp_single_normalise_denormal(&vsm);

	vdd.sign = vsm.sign;
	vdd.significand = (u64)vsm.significand << 32;

	/*
	 * If we have an infinity or NaN, the exponent must be 2047.
	 */
	if (tm & (VFP_INFINITY|VFP_NAN)) {
		vdd.exponent = 2047;
		if (tm & VFP_NAN)
			vdd.significand |= VFP_DOUBLE_SIGNIFICAND_QNAN;
		goto pack_nan;
	} else if (tm & VFP_ZERO)
		vdd.exponent = 0;
	else
		vdd.exponent = vsm.exponent + (1023 - 127);

	/*
	 * Technically, if bit 0 of dd is set, this is an invalid
	 * instruction.  However, we ignore this for efficiency.
	 */
	return vfp_double_normaliseround(dd, &vdd, fpscr, exceptions, "fcvtd");

 pack_nan:
	vfp_put_double(dd, vfp_double_pack(&vdd));
	return exceptions;
}

static u32 vfp_single_fuito(int sd, int unused, s32 m, u32 fpscr)
{
	struct vfp_single vs;

	vs.sign = 0;
	vs.exponent = 127 + 31 - 1;
	vs.significand = (u32)m;

	return vfp_single_normaliseround(sd, &vs, fpscr, 0, "fuito");
}

static u32 vfp_single_fsito(int sd, int unused, s32 m, u32 fpscr)
{
	struct vfp_single vs;

	vs.sign = (m & 0x80000000) >> 16;
	vs.exponent = 127 + 31 - 1;
	vs.significand = vs.sign ? -m : m;

	return vfp_single_normaliseround(sd, &vs, fpscr, 0, "fsito");
}

static u32 vfp_single_ftoui(int sd, int unused, s32 m, u32 fpscr)
{
	struct vfp_single vsm;
	u32 d, exceptions = 0;
	int rmode = fpscr & FPSCR_RMODE_MASK;
	int tm;

	vfp_single_unpack(&vsm, m);
	vfp_single_dump("VSM", &vsm);

	/*
	 * Do we have a denormalised number?
	 */
	tm = vfp_single_type(&vsm);
	if (tm & VFP_DENORMAL)
		exceptions |= FPSCR_IDC;

	if (tm & VFP_NAN)
		vsm.sign = 0;

	if (vsm.exponent >= 127 + 32) {
		d = vsm.sign ? 0 : 0xffffffff;
		exceptions = FPSCR_IOC;
	} else if (vsm.exponent >= 127 - 1) {
		int shift = 127 + 31 - vsm.exponent;
		u32 rem, incr = 0;

		/*
		 * 2^0 <= m < 2^32-2^8
		 */
		d = (vsm.significand << 1) >> shift;
		rem = vsm.significand << (33 - shift);

		if (rmode == FPSCR_ROUND_NEAREST) {
			incr = 0x80000000;
			if ((d & 1) == 0)
				incr -= 1;
		} else if (rmode == FPSCR_ROUND_TOZERO) {
			incr = 0;
		} else if ((rmode == FPSCR_ROUND_PLUSINF) ^ (vsm.sign != 0)) {
			incr = ~0;
		}

		if ((rem + incr) < rem) {
			if (d < 0xffffffff)
				d += 1;
			else
				exceptions |= FPSCR_IOC;
		}

		if (d && vsm.sign) {
			d = 0;
			exceptions |= FPSCR_IOC;
		} else if (rem)
			exceptions |= FPSCR_IXC;
	} else {
		d = 0;
		if (vsm.exponent | vsm.significand) {
			exceptions |= FPSCR_IXC;
			if (rmode == FPSCR_ROUND_PLUSINF && vsm.sign == 0)
				d = 1;
			else if (rmode == FPSCR_ROUND_MINUSINF && vsm.sign) {
				d = 0;
				exceptions |= FPSCR_IOC;
			}
		}
	}

	pr_debug("VFP: ftoui: d(s%d)=%08x exceptions=%08x\n", sd, d, exceptions);

	vfp_put_float(sd, d);

	return exceptions;
}

static u32 vfp_single_ftouiz(int sd, int unused, s32 m, u32 fpscr)
{
	return vfp_single_ftoui(sd, unused, m, FPSCR_ROUND_TOZERO);
}

static u32 vfp_single_ftosi(int sd, int unused, s32 m, u32 fpscr)
{
	struct vfp_single vsm;
	u32 d, exceptions = 0;
	int rmode = fpscr & FPSCR_RMODE_MASK;

	vfp_single_unpack(&vsm, m);
	vfp_single_dump("VSM", &vsm);

	/*
	 * Do we have a denormalised number?
	 */
	if (vfp_single_type(&vsm) & VFP_DENORMAL)
		exceptions |= FPSCR_IDC;

	if (vsm.exponent >= 127 + 32) {
		/*
		 * m >= 2^31-2^7: invalid
		 */
		d = 0x7fffffff;
		if (vsm.sign)
			d = ~d;
		exceptions |= FPSCR_IOC;
	} else if (vsm.exponent >= 127 - 1) {
		int shift = 127 + 31 - vsm.exponent;
		u32 rem, incr = 0;

		/* 2^0 <= m <= 2^31-2^7 */
		d = (vsm.significand << 1) >> shift;
		rem = vsm.significand << (33 - shift);

		if (rmode == FPSCR_ROUND_NEAREST) {
			incr = 0x80000000;
			if ((d & 1) == 0)
				incr -= 1;
		} else if (rmode == FPSCR_ROUND_TOZERO) {
			incr = 0;
		} else if ((rmode == FPSCR_ROUND_PLUSINF) ^ (vsm.sign != 0)) {
			incr = ~0;
		}

		if ((rem + incr) < rem && d < 0xffffffff)
			d += 1;
		if (d > 0x7fffffff + (vsm.sign != 0)) {
			d = 0x7fffffff + (vsm.sign != 0);
			exceptions |= FPSCR_IOC;
		} else if (rem)
			exceptions |= FPSCR_IXC;

		if (vsm.sign)
			d = -d;
	} else {
		d = 0;
		if (vsm.exponent | vsm.significand) {
			exceptions |= FPSCR_IXC;
			if (rmode == FPSCR_ROUND_PLUSINF && vsm.sign == 0)
				d = 1;
			else if (rmode == FPSCR_ROUND_MINUSINF && vsm.sign)
				d = -1;
		}
	}

	pr_debug("VFP: ftosi: d(s%d)=%08x exceptions=%08x\n", sd, d, exceptions);

	vfp_put_float(sd, (s32)d);

	return exceptions;
}

static u32 vfp_single_ftosiz(int sd, int unused, s32 m, u32 fpscr)
{
	return vfp_single_ftosi(sd, unused, m, FPSCR_ROUND_TOZERO);
}

static u32 (* const fop_extfns[32])(int sd, int unused, s32 m, u32 fpscr) = {
	[FEXT_TO_IDX(FEXT_FCPY)]	= vfp_single_fcpy,
	[FEXT_TO_IDX(FEXT_FABS)]	= vfp_single_fabs,
	[FEXT_TO_IDX(FEXT_FNEG)]	= vfp_single_fneg,
	[FEXT_TO_IDX(FEXT_FSQRT)]	= vfp_single_fsqrt,
	[FEXT_TO_IDX(FEXT_FCMP)]	= vfp_single_fcmp,
	[FEXT_TO_IDX(FEXT_FCMPE)]	= vfp_single_fcmpe,
	[FEXT_TO_IDX(FEXT_FCMPZ)]	= vfp_single_fcmpz,
	[FEXT_TO_IDX(FEXT_FCMPEZ)]	= vfp_single_fcmpez,
	[FEXT_TO_IDX(FEXT_FCVT)]	= vfp_single_fcvtd,
	[FEXT_TO_IDX(FEXT_FUITO)]	= vfp_single_fuito,
	[FEXT_TO_IDX(FEXT_FSITO)]	= vfp_single_fsito,
	[FEXT_TO_IDX(FEXT_FTOUI)]	= vfp_single_ftoui,
	[FEXT_TO_IDX(FEXT_FTOUIZ)]	= vfp_single_ftouiz,
	[FEXT_TO_IDX(FEXT_FTOSI)]	= vfp_single_ftosi,
	[FEXT_TO_IDX(FEXT_FTOSIZ)]	= vfp_single_ftosiz,
};





static u32
vfp_single_fadd_nonnumber(struct vfp_single *vsd, struct vfp_single *vsn,
			  struct vfp_single *vsm, u32 fpscr)
{
	struct vfp_single *vsp;
	u32 exceptions = 0;
	int tn, tm;

	tn = vfp_single_type(vsn);
	tm = vfp_single_type(vsm);

	if (tn & tm & VFP_INFINITY) {
		/*
		 * Two infinities.  Are they different signs?
		 */
		if (vsn->sign ^ vsm->sign) {
			/*
			 * different signs -> invalid
			 */
			exceptions = FPSCR_IOC;
			vsp = &vfp_single_default_qnan;
		} else {
			/*
			 * same signs -> valid
			 */
			vsp = vsn;
		}
	} else if (tn & VFP_INFINITY && tm & VFP_NUMBER) {
		/*
		 * One infinity and one number -> infinity
		 */
		vsp = vsn;
	} else {
		/*
		 * 'n' is a NaN of some type
		 */
		return vfp_propagate_nan(vsd, vsn, vsm, fpscr);
	}
	*vsd = *vsp;
	return exceptions;
}

static u32
vfp_single_add(struct vfp_single *vsd, struct vfp_single *vsn,
	       struct vfp_single *vsm, u32 fpscr)
{
	u32 exp_diff, m_sig;

	if (vsn->significand & 0x80000000 ||
	    vsm->significand & 0x80000000) {
		pr_info("VFP: bad FP values in %s\n", __func__);
		vfp_single_dump("VSN", vsn);
		vfp_single_dump("VSM", vsm);
	}

	/*
	 * Ensure that 'n' is the largest magnitude number.  Note that
	 * if 'n' and 'm' have equal exponents, we do not swap them.
	 * This ensures that NaN propagation works correctly.
	 */
	if (vsn->exponent < vsm->exponent) {
		struct vfp_single *t = vsn;
		vsn = vsm;
		vsm = t;
	}

	/*
	 * Is 'n' an infinity or a NaN?  Note that 'm' may be a number,
	 * infinity or a NaN here.
	 */
	if (vsn->exponent == 255)
		return vfp_single_fadd_nonnumber(vsd, vsn, vsm, fpscr);

	/*
	 * We have two proper numbers, where 'vsn' is the larger magnitude.
	 *
	 * Copy 'n' to 'd' before doing the arithmetic.
	 */
	*vsd = *vsn;

	/*
	 * Align both numbers.
	 */
	exp_diff = vsn->exponent - vsm->exponent;
	m_sig = vfp_shiftright32jamming(vsm->significand, exp_diff);

	/*
	 * If the signs are different, we are really subtracting.
	 */
	if (vsn->sign ^ vsm->sign) {
		m_sig = vsn->significand - m_sig;
		if ((s32)m_sig < 0) {
			vsd->sign = vfp_sign_negate(vsd->sign);
			m_sig = -m_sig;
		} else if (m_sig == 0) {
			vsd->sign = (fpscr & FPSCR_RMODE_MASK) ==
				      FPSCR_ROUND_MINUSINF ? 0x8000 : 0;
		}
	} else {
		m_sig = vsn->significand + m_sig;
	}
	vsd->significand = m_sig;

	return 0;
}

static u32
vfp_single_multiply(struct vfp_single *vsd, struct vfp_single *vsn, struct vfp_single *vsm, u32 fpscr)
{
	vfp_single_dump("VSN", vsn);
	vfp_single_dump("VSM", vsm);

	/*
	 * Ensure that 'n' is the largest magnitude number.  Note that
	 * if 'n' and 'm' have equal exponents, we do not swap them.
	 * This ensures that NaN propagation works correctly.
	 */
	if (vsn->exponent < vsm->exponent) {
		struct vfp_single *t = vsn;
		vsn = vsm;
		vsm = t;
		pr_debug("VFP: swapping M <-> N\n");
	}

	vsd->sign = vsn->sign ^ vsm->sign;

	/*
	 * If 'n' is an infinity or NaN, handle it.  'm' may be anything.
	 */
	if (vsn->exponent == 255) {
		if (vsn->significand || (vsm->exponent == 255 && vsm->significand))
			return vfp_propagate_nan(vsd, vsn, vsm, fpscr);
		if ((vsm->exponent | vsm->significand) == 0) {
			*vsd = vfp_single_default_qnan;
			return FPSCR_IOC;
		}
		vsd->exponent = vsn->exponent;
		vsd->significand = 0;
		return 0;
	}

	/*
	 * If 'm' is zero, the result is always zero.  In this case,
	 * 'n' may be zero or a number, but it doesn't matter which.
	 */
	if ((vsm->exponent | vsm->significand) == 0) {
		vsd->exponent = 0;
		vsd->significand = 0;
		return 0;
	}

	/*
	 * We add 2 to the destination exponent for the same reason as
	 * the addition case - though this time we have +1 from each
	 * input operand.
	 */
	vsd->exponent = vsn->exponent + vsm->exponent - 127 + 2;
	vsd->significand = vfp_hi64to32jamming((u64)vsn->significand * vsm->significand);

	vfp_single_dump("VSD", vsd);
	return 0;
}

#define NEG_MULTIPLY	(1 << 0)
#define NEG_SUBTRACT	(1 << 1)

static u32
vfp_single_multiply_accumulate(int sd, int sn, s32 m, u32 fpscr, u32 negate, char *func)
{
	struct vfp_single vsd, vsp, vsn, vsm;
	u32 exceptions;
	s32 v;

	v = vfp_get_float(sn);
	pr_debug("VFP: s%u = %08x\n", sn, v);
	vfp_single_unpack(&vsn, v);
	if (vsn.exponent == 0 && vsn.significand)
		vfp_single_normalise_denormal(&vsn);

	vfp_single_unpack(&vsm, m);
	if (vsm.exponent == 0 && vsm.significand)
		vfp_single_normalise_denormal(&vsm);

	exceptions = vfp_single_multiply(&vsp, &vsn, &vsm, fpscr);
	if (negate & NEG_MULTIPLY)
		vsp.sign = vfp_sign_negate(vsp.sign);

	v = vfp_get_float(sd);
	pr_debug("VFP: s%u = %08x\n", sd, v);
	vfp_single_unpack(&vsn, v);
	if (negate & NEG_SUBTRACT)
		vsn.sign = vfp_sign_negate(vsn.sign);

	exceptions |= vfp_single_add(&vsd, &vsn, &vsp, fpscr);

	return vfp_single_normaliseround(sd, &vsd, fpscr, exceptions, func);
}

/*
 * Standard operations
 */

/*
 * sd = sd + (sn * sm)
 */
static u32 vfp_single_fmac(int sd, int sn, s32 m, u32 fpscr)
{
	return vfp_single_multiply_accumulate(sd, sn, m, fpscr, 0, "fmac");
}

/*
 * sd = sd - (sn * sm)
 */
static u32 vfp_single_fnmac(int sd, int sn, s32 m, u32 fpscr)
{
	return vfp_single_multiply_accumulate(sd, sn, m, fpscr, NEG_MULTIPLY, "fnmac");
}

/*
 * sd = -sd + (sn * sm)
 */
static u32 vfp_single_fmsc(int sd, int sn, s32 m, u32 fpscr)
{
	return vfp_single_multiply_accumulate(sd, sn, m, fpscr, NEG_SUBTRACT, "fmsc");
}

/*
 * sd = -sd - (sn * sm)
 */
static u32 vfp_single_fnmsc(int sd, int sn, s32 m, u32 fpscr)
{
	return vfp_single_multiply_accumulate(sd, sn, m, fpscr, NEG_SUBTRACT | NEG_MULTIPLY, "fnmsc");
}

/*
 * sd = sn * sm
 */
static u32 vfp_single_fmul(int sd, int sn, s32 m, u32 fpscr)
{
	struct vfp_single vsd, vsn, vsm;
	u32 exceptions;
	s32 n = vfp_get_float(sn);

	pr_debug("VFP: s%u = %08x\n", sn, n);

	vfp_single_unpack(&vsn, n);
	if (vsn.exponent == 0 && vsn.significand)
		vfp_single_normalise_denormal(&vsn);

	vfp_single_unpack(&vsm, m);
	if (vsm.exponent == 0 && vsm.significand)
		vfp_single_normalise_denormal(&vsm);

	exceptions = vfp_single_multiply(&vsd, &vsn, &vsm, fpscr);
	return vfp_single_normaliseround(sd, &vsd, fpscr, exceptions, "fmul");
}

/*
 * sd = -(sn * sm)
 */
static u32 vfp_single_fnmul(int sd, int sn, s32 m, u32 fpscr)
{
	struct vfp_single vsd, vsn, vsm;
	u32 exceptions;
	s32 n = vfp_get_float(sn);

	pr_debug("VFP: s%u = %08x\n", sn, n);

	vfp_single_unpack(&vsn, n);
	if (vsn.exponent == 0 && vsn.significand)
		vfp_single_normalise_denormal(&vsn);

	vfp_single_unpack(&vsm, m);
	if (vsm.exponent == 0 && vsm.significand)
		vfp_single_normalise_denormal(&vsm);

	exceptions = vfp_single_multiply(&vsd, &vsn, &vsm, fpscr);
	vsd.sign = vfp_sign_negate(vsd.sign);
	return vfp_single_normaliseround(sd, &vsd, fpscr, exceptions, "fnmul");
}

/*
 * sd = sn + sm
 */
static u32 vfp_single_fadd(int sd, int sn, s32 m, u32 fpscr)
{
	struct vfp_single vsd, vsn, vsm;
	u32 exceptions;
	s32 n = vfp_get_float(sn);

	pr_debug("VFP: s%u = %08x\n", sn, n);

	/*
	 * Unpack and normalise denormals.
	 */
	vfp_single_unpack(&vsn, n);
	if (vsn.exponent == 0 && vsn.significand)
		vfp_single_normalise_denormal(&vsn);

	vfp_single_unpack(&vsm, m);
	if (vsm.exponent == 0 && vsm.significand)
		vfp_single_normalise_denormal(&vsm);

	exceptions = vfp_single_add(&vsd, &vsn, &vsm, fpscr);

	return vfp_single_normaliseround(sd, &vsd, fpscr, exceptions, "fadd");
}

/*
 * sd = sn - sm
 */
static u32 vfp_single_fsub(int sd, int sn, s32 m, u32 fpscr)
{
	/*
	 * Subtraction is addition with one sign inverted.
	 */
	return vfp_single_fadd(sd, sn, vfp_single_packed_negate(m), fpscr);
}

/*
 * sd = sn / sm
 */
static u32 vfp_single_fdiv(int sd, int sn, s32 m, u32 fpscr)
{
	struct vfp_single vsd, vsn, vsm;
	u32 exceptions = 0;
	s32 n = vfp_get_float(sn);
	int tm, tn;

	pr_debug("VFP: s%u = %08x\n", sn, n);

	vfp_single_unpack(&vsn, n);
	vfp_single_unpack(&vsm, m);

	vsd.sign = vsn.sign ^ vsm.sign;

	tn = vfp_single_type(&vsn);
	tm = vfp_single_type(&vsm);

	/*
	 * Is n a NAN?
	 */
	if (tn & VFP_NAN)
		goto vsn_nan;

	/*
	 * Is m a NAN?
	 */
	if (tm & VFP_NAN)
		goto vsm_nan;

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
	 * If m is zero, raise div0 exception
	 */
	if (tm & VFP_ZERO)
		goto divzero;

	/*
	 * If m is infinity, or n is zero, the result is zero
	 */
	if (tm & VFP_INFINITY || tn & VFP_ZERO)
		goto zero;

	if (tn & VFP_DENORMAL)
		vfp_single_normalise_denormal(&vsn);
	if (tm & VFP_DENORMAL)
		vfp_single_normalise_denormal(&vsm);

	/*
	 * Ok, we have two numbers, we can perform division.
	 */
	vsd.exponent = vsn.exponent - vsm.exponent + 127 - 1;
	vsm.significand <<= 1;
	if (vsm.significand <= (2 * vsn.significand)) {
		vsn.significand >>= 1;
		vsd.exponent++;
	}
	{
		u64 significand = (u64)vsn.significand << 32;
		do_div(significand, vsm.significand);
		vsd.significand = significand;
	}
	if ((vsd.significand & 0x3f) == 0)
		vsd.significand |= ((u64)vsm.significand * vsd.significand != (u64)vsn.significand << 32);

	return vfp_single_normaliseround(sd, &vsd, fpscr, 0, "fdiv");

 vsn_nan:
	exceptions = vfp_propagate_nan(&vsd, &vsn, &vsm, fpscr);
 pack:
	vfp_put_float(sd, vfp_single_pack(&vsd));
	return exceptions;

 vsm_nan:
	exceptions = vfp_propagate_nan(&vsd, &vsm, &vsn, fpscr);
	goto pack;

 zero:
	vsd.exponent = 0;
	vsd.significand = 0;
	goto pack;

 divzero:
	exceptions = FPSCR_DZC;
 infinity:
	vsd.exponent = 255;
	vsd.significand = 0;
	goto pack;

 invalid:
	vfp_put_float(sd, vfp_single_pack(&vfp_single_default_qnan));
	return FPSCR_IOC;
}

static u32 (* const fop_fns[16])(int sd, int sn, s32 m, u32 fpscr) = {
	[FOP_TO_IDX(FOP_FMAC)]	= vfp_single_fmac,
	[FOP_TO_IDX(FOP_FNMAC)]	= vfp_single_fnmac,
	[FOP_TO_IDX(FOP_FMSC)]	= vfp_single_fmsc,
	[FOP_TO_IDX(FOP_FNMSC)]	= vfp_single_fnmsc,
	[FOP_TO_IDX(FOP_FMUL)]	= vfp_single_fmul,
	[FOP_TO_IDX(FOP_FNMUL)]	= vfp_single_fnmul,
	[FOP_TO_IDX(FOP_FADD)]	= vfp_single_fadd,
	[FOP_TO_IDX(FOP_FSUB)]	= vfp_single_fsub,
	[FOP_TO_IDX(FOP_FDIV)]	= vfp_single_fdiv,
};

#define FREG_BANK(x)	((x) & 0x18)
#define FREG_IDX(x)	((x) & 7)

u32 vfp_single_cpdo(u32 inst, u32 fpscr)
{
	u32 op = inst & FOP_MASK;
	u32 exceptions = 0;
	unsigned int sd = vfp_get_sd(inst);
	unsigned int sn = vfp_get_sn(inst);
	unsigned int sm = vfp_get_sm(inst);
	unsigned int vecitr, veclen, vecstride;
	u32 (*fop)(int, int, s32, u32);

	veclen = fpscr & FPSCR_LENGTH_MASK;
	vecstride = 1 + ((fpscr & FPSCR_STRIDE_MASK) == FPSCR_STRIDE_MASK);

	/*
	 * If destination bank is zero, vector length is always '1'.
	 * ARM DDI0100F C5.1.3, C5.3.2.
	 */
	if (FREG_BANK(sd) == 0)
		veclen = 0;

	pr_debug("VFP: vecstride=%u veclen=%u\n", vecstride,
		 (veclen >> FPSCR_LENGTH_BIT) + 1);

	fop = (op == FOP_EXT) ? fop_extfns[sn] : fop_fns[FOP_TO_IDX(op)];
	if (!fop)
		goto invalid;

	for (vecitr = 0; vecitr <= veclen; vecitr += 1 << FPSCR_LENGTH_BIT) {
		s32 m = vfp_get_float(sm);
		u32 except;

		if (op == FOP_EXT)
			pr_debug("VFP: itr%d (s%u) = op[%u] (s%u=%08x)\n",
				 vecitr >> FPSCR_LENGTH_BIT, sd, sn, sm, m);
		else
			pr_debug("VFP: itr%d (s%u) = (s%u) op[%u] (s%u=%08x)\n",
				 vecitr >> FPSCR_LENGTH_BIT, sd, sn,
				 FOP_TO_IDX(op), sm, m);

		except = fop(sd, sn, m, fpscr);
		pr_debug("VFP: itr%d: exceptions=%08x\n",
			 vecitr >> FPSCR_LENGTH_BIT, except);

		exceptions |= except;

		/*
		 * This ensures that comparisons only operate on scalars;
		 * comparisons always return with one FPSCR status bit set.
		 */
		if (except & (FPSCR_N|FPSCR_Z|FPSCR_C|FPSCR_V))
			break;

		/*
		 * CHECK: It appears to be undefined whether we stop when
		 * we encounter an exception.  We continue.
		 */

		sd = FREG_BANK(sd) + ((FREG_IDX(sd) + vecstride) & 7);
		sn = FREG_BANK(sn) + ((FREG_IDX(sn) + vecstride) & 7);
		if (FREG_BANK(sm) != 0)
			sm = FREG_BANK(sm) + ((FREG_IDX(sm) + vecstride) & 7);
	}
	return exceptions;

 invalid:
	return (u32)-1;
}
