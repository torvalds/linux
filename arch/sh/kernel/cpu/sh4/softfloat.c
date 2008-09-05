/*
 * Floating point emulation support for subnormalised numbers on SH4
 * architecture This file is derived from the SoftFloat IEC/IEEE
 * Floating-point Arithmetic Package, Release 2 the original license of
 * which is reproduced below.
 *
 * ========================================================================
 *
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
 *
 * ========================================================================
 *
 * SH4 modifications by Ismail Dhaoui <ismail.dhaoui@st.com>
 * and Kamel Khelifi <kamel.khelifi@st.com>
 */
#include <linux/kernel.h>
#include <cpu/fpu.h>

#define LIT64( a ) a##LL

typedef char flag;
typedef unsigned char uint8;
typedef signed char int8;
typedef int uint16;
typedef int int16;
typedef unsigned int uint32;
typedef signed int int32;

typedef unsigned long long int bits64;
typedef signed long long int sbits64;

typedef unsigned char bits8;
typedef signed char sbits8;
typedef unsigned short int bits16;
typedef signed short int sbits16;
typedef unsigned int bits32;
typedef signed int sbits32;

typedef unsigned long long int uint64;
typedef signed long long int int64;

typedef unsigned long int float32;
typedef unsigned long long float64;

extern void float_raise(unsigned int flags);	/* in fpu.c */
extern int float_rounding_mode(void);	/* in fpu.c */

inline bits64 extractFloat64Frac(float64 a);
inline flag extractFloat64Sign(float64 a);
inline int16 extractFloat64Exp(float64 a);
inline int16 extractFloat32Exp(float32 a);
inline flag extractFloat32Sign(float32 a);
inline bits32 extractFloat32Frac(float32 a);
inline float64 packFloat64(flag zSign, int16 zExp, bits64 zSig);
inline void shift64RightJamming(bits64 a, int16 count, bits64 * zPtr);
inline float32 packFloat32(flag zSign, int16 zExp, bits32 zSig);
inline void shift32RightJamming(bits32 a, int16 count, bits32 * zPtr);
float64 float64_sub(float64 a, float64 b);
float32 float32_sub(float32 a, float32 b);
float32 float32_add(float32 a, float32 b);
float64 float64_add(float64 a, float64 b);
float64 float64_div(float64 a, float64 b);
float32 float32_div(float32 a, float32 b);
float32 float32_mul(float32 a, float32 b);
float64 float64_mul(float64 a, float64 b);
float32 float64_to_float32(float64 a);
inline void add128(bits64 a0, bits64 a1, bits64 b0, bits64 b1, bits64 * z0Ptr,
		   bits64 * z1Ptr);
inline void sub128(bits64 a0, bits64 a1, bits64 b0, bits64 b1, bits64 * z0Ptr,
		   bits64 * z1Ptr);
inline void mul64To128(bits64 a, bits64 b, bits64 * z0Ptr, bits64 * z1Ptr);

static int8 countLeadingZeros32(bits32 a);
static int8 countLeadingZeros64(bits64 a);
static float64 normalizeRoundAndPackFloat64(flag zSign, int16 zExp,
					    bits64 zSig);
static float64 subFloat64Sigs(float64 a, float64 b, flag zSign);
static float64 addFloat64Sigs(float64 a, float64 b, flag zSign);
static float32 roundAndPackFloat32(flag zSign, int16 zExp, bits32 zSig);
static float32 normalizeRoundAndPackFloat32(flag zSign, int16 zExp,
					    bits32 zSig);
static float64 roundAndPackFloat64(flag zSign, int16 zExp, bits64 zSig);
static float32 subFloat32Sigs(float32 a, float32 b, flag zSign);
static float32 addFloat32Sigs(float32 a, float32 b, flag zSign);
static void normalizeFloat64Subnormal(bits64 aSig, int16 * zExpPtr,
				      bits64 * zSigPtr);
static bits64 estimateDiv128To64(bits64 a0, bits64 a1, bits64 b);
static void normalizeFloat32Subnormal(bits32 aSig, int16 * zExpPtr,
				      bits32 * zSigPtr);

inline bits64 extractFloat64Frac(float64 a)
{
	return a & LIT64(0x000FFFFFFFFFFFFF);
}

inline flag extractFloat64Sign(float64 a)
{
	return a >> 63;
}

inline int16 extractFloat64Exp(float64 a)
{
	return (a >> 52) & 0x7FF;
}

inline int16 extractFloat32Exp(float32 a)
{
	return (a >> 23) & 0xFF;
}

inline flag extractFloat32Sign(float32 a)
{
	return a >> 31;
}

inline bits32 extractFloat32Frac(float32 a)
{
	return a & 0x007FFFFF;
}

inline float64 packFloat64(flag zSign, int16 zExp, bits64 zSig)
{
	return (((bits64) zSign) << 63) + (((bits64) zExp) << 52) + zSig;
}

inline void shift64RightJamming(bits64 a, int16 count, bits64 * zPtr)
{
	bits64 z;

	if (count == 0) {
		z = a;
	} else if (count < 64) {
		z = (a >> count) | ((a << ((-count) & 63)) != 0);
	} else {
		z = (a != 0);
	}
	*zPtr = z;
}

static int8 countLeadingZeros32(bits32 a)
{
	static const int8 countLeadingZerosHigh[] = {
		8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4,
		3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	int8 shiftCount;

	shiftCount = 0;
	if (a < 0x10000) {
		shiftCount += 16;
		a <<= 16;
	}
	if (a < 0x1000000) {
		shiftCount += 8;
		a <<= 8;
	}
	shiftCount += countLeadingZerosHigh[a >> 24];
	return shiftCount;

}

static int8 countLeadingZeros64(bits64 a)
{
	int8 shiftCount;

	shiftCount = 0;
	if (a < ((bits64) 1) << 32) {
		shiftCount += 32;
	} else {
		a >>= 32;
	}
	shiftCount += countLeadingZeros32(a);
	return shiftCount;

}

static float64 normalizeRoundAndPackFloat64(flag zSign, int16 zExp, bits64 zSig)
{
	int8 shiftCount;

	shiftCount = countLeadingZeros64(zSig) - 1;
	return roundAndPackFloat64(zSign, zExp - shiftCount,
				   zSig << shiftCount);

}

static float64 subFloat64Sigs(float64 a, float64 b, flag zSign)
{
	int16 aExp, bExp, zExp;
	bits64 aSig, bSig, zSig;
	int16 expDiff;

	aSig = extractFloat64Frac(a);
	aExp = extractFloat64Exp(a);
	bSig = extractFloat64Frac(b);
	bExp = extractFloat64Exp(b);
	expDiff = aExp - bExp;
	aSig <<= 10;
	bSig <<= 10;
	if (0 < expDiff)
		goto aExpBigger;
	if (expDiff < 0)
		goto bExpBigger;
	if (aExp == 0) {
		aExp = 1;
		bExp = 1;
	}
	if (bSig < aSig)
		goto aBigger;
	if (aSig < bSig)
		goto bBigger;
	return packFloat64(float_rounding_mode() == FPSCR_RM_ZERO, 0, 0);
      bExpBigger:
	if (bExp == 0x7FF) {
		return packFloat64(zSign ^ 1, 0x7FF, 0);
	}
	if (aExp == 0) {
		++expDiff;
	} else {
		aSig |= LIT64(0x4000000000000000);
	}
	shift64RightJamming(aSig, -expDiff, &aSig);
	bSig |= LIT64(0x4000000000000000);
      bBigger:
	zSig = bSig - aSig;
	zExp = bExp;
	zSign ^= 1;
	goto normalizeRoundAndPack;
      aExpBigger:
	if (aExp == 0x7FF) {
		return a;
	}
	if (bExp == 0) {
		--expDiff;
	} else {
		bSig |= LIT64(0x4000000000000000);
	}
	shift64RightJamming(bSig, expDiff, &bSig);
	aSig |= LIT64(0x4000000000000000);
      aBigger:
	zSig = aSig - bSig;
	zExp = aExp;
      normalizeRoundAndPack:
	--zExp;
	return normalizeRoundAndPackFloat64(zSign, zExp, zSig);

}
static float64 addFloat64Sigs(float64 a, float64 b, flag zSign)
{
	int16 aExp, bExp, zExp;
	bits64 aSig, bSig, zSig;
	int16 expDiff;

	aSig = extractFloat64Frac(a);
	aExp = extractFloat64Exp(a);
	bSig = extractFloat64Frac(b);
	bExp = extractFloat64Exp(b);
	expDiff = aExp - bExp;
	aSig <<= 9;
	bSig <<= 9;
	if (0 < expDiff) {
		if (aExp == 0x7FF) {
			return a;
		}
		if (bExp == 0) {
			--expDiff;
		} else {
			bSig |= LIT64(0x2000000000000000);
		}
		shift64RightJamming(bSig, expDiff, &bSig);
		zExp = aExp;
	} else if (expDiff < 0) {
		if (bExp == 0x7FF) {
			return packFloat64(zSign, 0x7FF, 0);
		}
		if (aExp == 0) {
			++expDiff;
		} else {
			aSig |= LIT64(0x2000000000000000);
		}
		shift64RightJamming(aSig, -expDiff, &aSig);
		zExp = bExp;
	} else {
		if (aExp == 0x7FF) {
			return a;
		}
		if (aExp == 0)
			return packFloat64(zSign, 0, (aSig + bSig) >> 9);
		zSig = LIT64(0x4000000000000000) + aSig + bSig;
		zExp = aExp;
		goto roundAndPack;
	}
	aSig |= LIT64(0x2000000000000000);
	zSig = (aSig + bSig) << 1;
	--zExp;
	if ((sbits64) zSig < 0) {
		zSig = aSig + bSig;
		++zExp;
	}
      roundAndPack:
	return roundAndPackFloat64(zSign, zExp, zSig);

}

inline float32 packFloat32(flag zSign, int16 zExp, bits32 zSig)
{
	return (((bits32) zSign) << 31) + (((bits32) zExp) << 23) + zSig;
}

inline void shift32RightJamming(bits32 a, int16 count, bits32 * zPtr)
{
	bits32 z;
	if (count == 0) {
		z = a;
	} else if (count < 32) {
		z = (a >> count) | ((a << ((-count) & 31)) != 0);
	} else {
		z = (a != 0);
	}
	*zPtr = z;
}

static float32 roundAndPackFloat32(flag zSign, int16 zExp, bits32 zSig)
{
	flag roundNearestEven;
	int8 roundIncrement, roundBits;
	flag isTiny;

	/* SH4 has only 2 rounding modes - round to nearest and round to zero */
	roundNearestEven = (float_rounding_mode() == FPSCR_RM_NEAREST);
	roundIncrement = 0x40;
	if (!roundNearestEven) {
		roundIncrement = 0;
	}
	roundBits = zSig & 0x7F;
	if (0xFD <= (bits16) zExp) {
		if ((0xFD < zExp)
		    || ((zExp == 0xFD)
			&& ((sbits32) (zSig + roundIncrement) < 0))
		    ) {
			float_raise(FPSCR_CAUSE_OVERFLOW | FPSCR_CAUSE_INEXACT);
			return packFloat32(zSign, 0xFF,
					   0) - (roundIncrement == 0);
		}
		if (zExp < 0) {
			isTiny = (zExp < -1)
			    || (zSig + roundIncrement < 0x80000000);
			shift32RightJamming(zSig, -zExp, &zSig);
			zExp = 0;
			roundBits = zSig & 0x7F;
			if (isTiny && roundBits)
				float_raise(FPSCR_CAUSE_UNDERFLOW);
		}
	}
	if (roundBits)
		float_raise(FPSCR_CAUSE_INEXACT);
	zSig = (zSig + roundIncrement) >> 7;
	zSig &= ~(((roundBits ^ 0x40) == 0) & roundNearestEven);
	if (zSig == 0)
		zExp = 0;
	return packFloat32(zSign, zExp, zSig);

}

static float32 normalizeRoundAndPackFloat32(flag zSign, int16 zExp, bits32 zSig)
{
	int8 shiftCount;

	shiftCount = countLeadingZeros32(zSig) - 1;
	return roundAndPackFloat32(zSign, zExp - shiftCount,
				   zSig << shiftCount);
}

static float64 roundAndPackFloat64(flag zSign, int16 zExp, bits64 zSig)
{
	flag roundNearestEven;
	int16 roundIncrement, roundBits;
	flag isTiny;

	/* SH4 has only 2 rounding modes - round to nearest and round to zero */
	roundNearestEven = (float_rounding_mode() == FPSCR_RM_NEAREST);
	roundIncrement = 0x200;
	if (!roundNearestEven) {
		roundIncrement = 0;
	}
	roundBits = zSig & 0x3FF;
	if (0x7FD <= (bits16) zExp) {
		if ((0x7FD < zExp)
		    || ((zExp == 0x7FD)
			&& ((sbits64) (zSig + roundIncrement) < 0))
		    ) {
			float_raise(FPSCR_CAUSE_OVERFLOW | FPSCR_CAUSE_INEXACT);
			return packFloat64(zSign, 0x7FF,
					   0) - (roundIncrement == 0);
		}
		if (zExp < 0) {
			isTiny = (zExp < -1)
			    || (zSig + roundIncrement <
				LIT64(0x8000000000000000));
			shift64RightJamming(zSig, -zExp, &zSig);
			zExp = 0;
			roundBits = zSig & 0x3FF;
			if (isTiny && roundBits)
				float_raise(FPSCR_CAUSE_UNDERFLOW);
		}
	}
	if (roundBits)
		float_raise(FPSCR_CAUSE_INEXACT);
	zSig = (zSig + roundIncrement) >> 10;
	zSig &= ~(((roundBits ^ 0x200) == 0) & roundNearestEven);
	if (zSig == 0)
		zExp = 0;
	return packFloat64(zSign, zExp, zSig);

}

static float32 subFloat32Sigs(float32 a, float32 b, flag zSign)
{
	int16 aExp, bExp, zExp;
	bits32 aSig, bSig, zSig;
	int16 expDiff;

	aSig = extractFloat32Frac(a);
	aExp = extractFloat32Exp(a);
	bSig = extractFloat32Frac(b);
	bExp = extractFloat32Exp(b);
	expDiff = aExp - bExp;
	aSig <<= 7;
	bSig <<= 7;
	if (0 < expDiff)
		goto aExpBigger;
	if (expDiff < 0)
		goto bExpBigger;
	if (aExp == 0) {
		aExp = 1;
		bExp = 1;
	}
	if (bSig < aSig)
		goto aBigger;
	if (aSig < bSig)
		goto bBigger;
	return packFloat32(float_rounding_mode() == FPSCR_RM_ZERO, 0, 0);
      bExpBigger:
	if (bExp == 0xFF) {
		return packFloat32(zSign ^ 1, 0xFF, 0);
	}
	if (aExp == 0) {
		++expDiff;
	} else {
		aSig |= 0x40000000;
	}
	shift32RightJamming(aSig, -expDiff, &aSig);
	bSig |= 0x40000000;
      bBigger:
	zSig = bSig - aSig;
	zExp = bExp;
	zSign ^= 1;
	goto normalizeRoundAndPack;
      aExpBigger:
	if (aExp == 0xFF) {
		return a;
	}
	if (bExp == 0) {
		--expDiff;
	} else {
		bSig |= 0x40000000;
	}
	shift32RightJamming(bSig, expDiff, &bSig);
	aSig |= 0x40000000;
      aBigger:
	zSig = aSig - bSig;
	zExp = aExp;
      normalizeRoundAndPack:
	--zExp;
	return normalizeRoundAndPackFloat32(zSign, zExp, zSig);

}

static float32 addFloat32Sigs(float32 a, float32 b, flag zSign)
{
	int16 aExp, bExp, zExp;
	bits32 aSig, bSig, zSig;
	int16 expDiff;

	aSig = extractFloat32Frac(a);
	aExp = extractFloat32Exp(a);
	bSig = extractFloat32Frac(b);
	bExp = extractFloat32Exp(b);
	expDiff = aExp - bExp;
	aSig <<= 6;
	bSig <<= 6;
	if (0 < expDiff) {
		if (aExp == 0xFF) {
			return a;
		}
		if (bExp == 0) {
			--expDiff;
		} else {
			bSig |= 0x20000000;
		}
		shift32RightJamming(bSig, expDiff, &bSig);
		zExp = aExp;
	} else if (expDiff < 0) {
		if (bExp == 0xFF) {
			return packFloat32(zSign, 0xFF, 0);
		}
		if (aExp == 0) {
			++expDiff;
		} else {
			aSig |= 0x20000000;
		}
		shift32RightJamming(aSig, -expDiff, &aSig);
		zExp = bExp;
	} else {
		if (aExp == 0xFF) {
			return a;
		}
		if (aExp == 0)
			return packFloat32(zSign, 0, (aSig + bSig) >> 6);
		zSig = 0x40000000 + aSig + bSig;
		zExp = aExp;
		goto roundAndPack;
	}
	aSig |= 0x20000000;
	zSig = (aSig + bSig) << 1;
	--zExp;
	if ((sbits32) zSig < 0) {
		zSig = aSig + bSig;
		++zExp;
	}
      roundAndPack:
	return roundAndPackFloat32(zSign, zExp, zSig);

}

float64 float64_sub(float64 a, float64 b)
{
	flag aSign, bSign;

	aSign = extractFloat64Sign(a);
	bSign = extractFloat64Sign(b);
	if (aSign == bSign) {
		return subFloat64Sigs(a, b, aSign);
	} else {
		return addFloat64Sigs(a, b, aSign);
	}

}

float32 float32_sub(float32 a, float32 b)
{
	flag aSign, bSign;

	aSign = extractFloat32Sign(a);
	bSign = extractFloat32Sign(b);
	if (aSign == bSign) {
		return subFloat32Sigs(a, b, aSign);
	} else {
		return addFloat32Sigs(a, b, aSign);
	}

}

float32 float32_add(float32 a, float32 b)
{
	flag aSign, bSign;

	aSign = extractFloat32Sign(a);
	bSign = extractFloat32Sign(b);
	if (aSign == bSign) {
		return addFloat32Sigs(a, b, aSign);
	} else {
		return subFloat32Sigs(a, b, aSign);
	}

}

float64 float64_add(float64 a, float64 b)
{
	flag aSign, bSign;

	aSign = extractFloat64Sign(a);
	bSign = extractFloat64Sign(b);
	if (aSign == bSign) {
		return addFloat64Sigs(a, b, aSign);
	} else {
		return subFloat64Sigs(a, b, aSign);
	}
}

static void
normalizeFloat64Subnormal(bits64 aSig, int16 * zExpPtr, bits64 * zSigPtr)
{
	int8 shiftCount;

	shiftCount = countLeadingZeros64(aSig) - 11;
	*zSigPtr = aSig << shiftCount;
	*zExpPtr = 1 - shiftCount;
}

inline void add128(bits64 a0, bits64 a1, bits64 b0, bits64 b1, bits64 * z0Ptr,
		   bits64 * z1Ptr)
{
	bits64 z1;

	z1 = a1 + b1;
	*z1Ptr = z1;
	*z0Ptr = a0 + b0 + (z1 < a1);
}

inline void
sub128(bits64 a0, bits64 a1, bits64 b0, bits64 b1, bits64 * z0Ptr,
       bits64 * z1Ptr)
{
	*z1Ptr = a1 - b1;
	*z0Ptr = a0 - b0 - (a1 < b1);
}

static bits64 estimateDiv128To64(bits64 a0, bits64 a1, bits64 b)
{
	bits64 b0, b1;
	bits64 rem0, rem1, term0, term1;
	bits64 z;
	if (b <= a0)
		return LIT64(0xFFFFFFFFFFFFFFFF);
	b0 = b >> 32;
	z = (b0 << 32 <= a0) ? LIT64(0xFFFFFFFF00000000) : (a0 / b0) << 32;
	mul64To128(b, z, &term0, &term1);
	sub128(a0, a1, term0, term1, &rem0, &rem1);
	while (((sbits64) rem0) < 0) {
		z -= LIT64(0x100000000);
		b1 = b << 32;
		add128(rem0, rem1, b0, b1, &rem0, &rem1);
	}
	rem0 = (rem0 << 32) | (rem1 >> 32);
	z |= (b0 << 32 <= rem0) ? 0xFFFFFFFF : rem0 / b0;
	return z;
}

inline void mul64To128(bits64 a, bits64 b, bits64 * z0Ptr, bits64 * z1Ptr)
{
	bits32 aHigh, aLow, bHigh, bLow;
	bits64 z0, zMiddleA, zMiddleB, z1;

	aLow = a;
	aHigh = a >> 32;
	bLow = b;
	bHigh = b >> 32;
	z1 = ((bits64) aLow) * bLow;
	zMiddleA = ((bits64) aLow) * bHigh;
	zMiddleB = ((bits64) aHigh) * bLow;
	z0 = ((bits64) aHigh) * bHigh;
	zMiddleA += zMiddleB;
	z0 += (((bits64) (zMiddleA < zMiddleB)) << 32) + (zMiddleA >> 32);
	zMiddleA <<= 32;
	z1 += zMiddleA;
	z0 += (z1 < zMiddleA);
	*z1Ptr = z1;
	*z0Ptr = z0;

}

static void normalizeFloat32Subnormal(bits32 aSig, int16 * zExpPtr,
				      bits32 * zSigPtr)
{
	int8 shiftCount;

	shiftCount = countLeadingZeros32(aSig) - 8;
	*zSigPtr = aSig << shiftCount;
	*zExpPtr = 1 - shiftCount;

}

float64 float64_div(float64 a, float64 b)
{
	flag aSign, bSign, zSign;
	int16 aExp, bExp, zExp;
	bits64 aSig, bSig, zSig;
	bits64 rem0, rem1;
	bits64 term0, term1;

	aSig = extractFloat64Frac(a);
	aExp = extractFloat64Exp(a);
	aSign = extractFloat64Sign(a);
	bSig = extractFloat64Frac(b);
	bExp = extractFloat64Exp(b);
	bSign = extractFloat64Sign(b);
	zSign = aSign ^ bSign;
	if (aExp == 0x7FF) {
		if (bExp == 0x7FF) {
		}
		return packFloat64(zSign, 0x7FF, 0);
	}
	if (bExp == 0x7FF) {
		return packFloat64(zSign, 0, 0);
	}
	if (bExp == 0) {
		if (bSig == 0) {
			if ((aExp | aSig) == 0) {
				float_raise(FPSCR_CAUSE_INVALID);
			}
			return packFloat64(zSign, 0x7FF, 0);
		}
		normalizeFloat64Subnormal(bSig, &bExp, &bSig);
	}
	if (aExp == 0) {
		if (aSig == 0)
			return packFloat64(zSign, 0, 0);
		normalizeFloat64Subnormal(aSig, &aExp, &aSig);
	}
	zExp = aExp - bExp + 0x3FD;
	aSig = (aSig | LIT64(0x0010000000000000)) << 10;
	bSig = (bSig | LIT64(0x0010000000000000)) << 11;
	if (bSig <= (aSig + aSig)) {
		aSig >>= 1;
		++zExp;
	}
	zSig = estimateDiv128To64(aSig, 0, bSig);
	if ((zSig & 0x1FF) <= 2) {
		mul64To128(bSig, zSig, &term0, &term1);
		sub128(aSig, 0, term0, term1, &rem0, &rem1);
		while ((sbits64) rem0 < 0) {
			--zSig;
			add128(rem0, rem1, 0, bSig, &rem0, &rem1);
		}
		zSig |= (rem1 != 0);
	}
	return roundAndPackFloat64(zSign, zExp, zSig);

}

float32 float32_div(float32 a, float32 b)
{
	flag aSign, bSign, zSign;
	int16 aExp, bExp, zExp;
	bits32 aSig, bSig, zSig;

	aSig = extractFloat32Frac(a);
	aExp = extractFloat32Exp(a);
	aSign = extractFloat32Sign(a);
	bSig = extractFloat32Frac(b);
	bExp = extractFloat32Exp(b);
	bSign = extractFloat32Sign(b);
	zSign = aSign ^ bSign;
	if (aExp == 0xFF) {
		if (bExp == 0xFF) {
		}
		return packFloat32(zSign, 0xFF, 0);
	}
	if (bExp == 0xFF) {
		return packFloat32(zSign, 0, 0);
	}
	if (bExp == 0) {
		if (bSig == 0) {
			return packFloat32(zSign, 0xFF, 0);
		}
		normalizeFloat32Subnormal(bSig, &bExp, &bSig);
	}
	if (aExp == 0) {
		if (aSig == 0)
			return packFloat32(zSign, 0, 0);
		normalizeFloat32Subnormal(aSig, &aExp, &aSig);
	}
	zExp = aExp - bExp + 0x7D;
	aSig = (aSig | 0x00800000) << 7;
	bSig = (bSig | 0x00800000) << 8;
	if (bSig <= (aSig + aSig)) {
		aSig >>= 1;
		++zExp;
	}
	zSig = (((bits64) aSig) << 32) / bSig;
	if ((zSig & 0x3F) == 0) {
		zSig |= (((bits64) bSig) * zSig != ((bits64) aSig) << 32);
	}
	return roundAndPackFloat32(zSign, zExp, zSig);

}

float32 float32_mul(float32 a, float32 b)
{
	char aSign, bSign, zSign;
	int aExp, bExp, zExp;
	unsigned int aSig, bSig;
	unsigned long long zSig64;
	unsigned int zSig;

	aSig = extractFloat32Frac(a);
	aExp = extractFloat32Exp(a);
	aSign = extractFloat32Sign(a);
	bSig = extractFloat32Frac(b);
	bExp = extractFloat32Exp(b);
	bSign = extractFloat32Sign(b);
	zSign = aSign ^ bSign;
	if (aExp == 0) {
		if (aSig == 0)
			return packFloat32(zSign, 0, 0);
		normalizeFloat32Subnormal(aSig, &aExp, &aSig);
	}
	if (bExp == 0) {
		if (bSig == 0)
			return packFloat32(zSign, 0, 0);
		normalizeFloat32Subnormal(bSig, &bExp, &bSig);
	}
	if ((bExp == 0xff && bSig == 0) || (aExp == 0xff && aSig == 0))
		return roundAndPackFloat32(zSign, 0xff, 0);

	zExp = aExp + bExp - 0x7F;
	aSig = (aSig | 0x00800000) << 7;
	bSig = (bSig | 0x00800000) << 8;
	shift64RightJamming(((unsigned long long)aSig) * bSig, 32, &zSig64);
	zSig = zSig64;
	if (0 <= (signed int)(zSig << 1)) {
		zSig <<= 1;
		--zExp;
	}
	return roundAndPackFloat32(zSign, zExp, zSig);

}

float64 float64_mul(float64 a, float64 b)
{
	char aSign, bSign, zSign;
	int aExp, bExp, zExp;
	unsigned long long int aSig, bSig, zSig0, zSig1;

	aSig = extractFloat64Frac(a);
	aExp = extractFloat64Exp(a);
	aSign = extractFloat64Sign(a);
	bSig = extractFloat64Frac(b);
	bExp = extractFloat64Exp(b);
	bSign = extractFloat64Sign(b);
	zSign = aSign ^ bSign;

	if (aExp == 0) {
		if (aSig == 0)
			return packFloat64(zSign, 0, 0);
		normalizeFloat64Subnormal(aSig, &aExp, &aSig);
	}
	if (bExp == 0) {
		if (bSig == 0)
			return packFloat64(zSign, 0, 0);
		normalizeFloat64Subnormal(bSig, &bExp, &bSig);
	}
	if ((aExp == 0x7ff && aSig == 0) || (bExp == 0x7ff && bSig == 0))
		return roundAndPackFloat64(zSign, 0x7ff, 0);

	zExp = aExp + bExp - 0x3FF;
	aSig = (aSig | 0x0010000000000000LL) << 10;
	bSig = (bSig | 0x0010000000000000LL) << 11;
	mul64To128(aSig, bSig, &zSig0, &zSig1);
	zSig0 |= (zSig1 != 0);
	if (0 <= (signed long long int)(zSig0 << 1)) {
		zSig0 <<= 1;
		--zExp;
	}
	return roundAndPackFloat64(zSign, zExp, zSig0);
}

/*
 * -------------------------------------------------------------------------------
 *  Returns the result of converting the double-precision floating-point value
 *  `a' to the single-precision floating-point format.  The conversion is
 *  performed according to the IEC/IEEE Standard for Binary Floating-point
 *  Arithmetic.
 *  -------------------------------------------------------------------------------
 *  */
float32 float64_to_float32(float64 a)
{
    flag aSign;
    int16 aExp;
    bits64 aSig;
    bits32 zSig;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );

    shift64RightJamming( aSig, 22, &aSig );
    zSig = aSig;
    if ( aExp || zSig ) {
        zSig |= 0x40000000;
        aExp -= 0x381;
    }
    return roundAndPackFloat32(aSign, aExp, zSig);
}
