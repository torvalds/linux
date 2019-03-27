/* This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 */

#include "DD.h"
#include "../int_math.h"
// Use DOUBLE_PRECISION because the soft-fp method we use is logb (on the upper
// half of the long doubles), even though this file defines complex division for
// 128-bit floats.
#define DOUBLE_PRECISION
#include "../fp_lib.h"

#if !defined(CRT_INFINITY) && defined(HUGE_VAL)
#define CRT_INFINITY HUGE_VAL
#endif /* CRT_INFINITY */

#define makeFinite(x) { \
    (x).s.hi = crt_copysign(crt_isinf((x).s.hi) ? 1.0 : 0.0, (x).s.hi); \
    (x).s.lo = 0.0;                                                     \
  }

long double _Complex
__divtc3(long double a, long double b, long double c, long double d)
{
	DD cDD = { .ld = c };
	DD dDD = { .ld = d };
	
	int ilogbw = 0;
	const double logbw = __compiler_rt_logb(
		crt_fmax(crt_fabs(cDD.s.hi), crt_fabs(dDD.s.hi)));

        if (crt_isfinite(logbw))
	{
		ilogbw = (int)logbw;
		
		cDD.s.hi = crt_scalbn(cDD.s.hi, -ilogbw);
		cDD.s.lo = crt_scalbn(cDD.s.lo, -ilogbw);
		dDD.s.hi = crt_scalbn(dDD.s.hi, -ilogbw);
		dDD.s.lo = crt_scalbn(dDD.s.lo, -ilogbw);
	}
	
	const long double denom = __gcc_qadd(__gcc_qmul(cDD.ld, cDD.ld), __gcc_qmul(dDD.ld, dDD.ld));
	const long double realNumerator = __gcc_qadd(__gcc_qmul(a,cDD.ld), __gcc_qmul(b,dDD.ld));
	const long double imagNumerator = __gcc_qsub(__gcc_qmul(b,cDD.ld), __gcc_qmul(a,dDD.ld));
	
	DD real = { .ld = __gcc_qdiv(realNumerator, denom) };
	DD imag = { .ld = __gcc_qdiv(imagNumerator, denom) };
	
	real.s.hi = crt_scalbn(real.s.hi, -ilogbw);
	real.s.lo = crt_scalbn(real.s.lo, -ilogbw);
	imag.s.hi = crt_scalbn(imag.s.hi, -ilogbw);
	imag.s.lo = crt_scalbn(imag.s.lo, -ilogbw);
	
	if (crt_isnan(real.s.hi) && crt_isnan(imag.s.hi))
	{
		DD aDD = { .ld = a };
		DD bDD = { .ld = b };
		DD rDD = { .ld = denom };
		
		if ((rDD.s.hi == 0.0) && (!crt_isnan(aDD.s.hi) ||
                                          !crt_isnan(bDD.s.hi)))
		{
			real.s.hi = crt_copysign(CRT_INFINITY,cDD.s.hi) * aDD.s.hi;
			real.s.lo = 0.0;
			imag.s.hi = crt_copysign(CRT_INFINITY,cDD.s.hi) * bDD.s.hi;
			imag.s.lo = 0.0;
		}
		
		else if ((crt_isinf(aDD.s.hi) || crt_isinf(bDD.s.hi)) &&
                         crt_isfinite(cDD.s.hi) && crt_isfinite(dDD.s.hi))
		{
			makeFinite(aDD);
			makeFinite(bDD);
			real.s.hi = CRT_INFINITY * (aDD.s.hi*cDD.s.hi + bDD.s.hi*dDD.s.hi);
			real.s.lo = 0.0;
			imag.s.hi = CRT_INFINITY * (bDD.s.hi*cDD.s.hi - aDD.s.hi*dDD.s.hi);
			imag.s.lo = 0.0;
		}
		
		else if ((crt_isinf(cDD.s.hi) || crt_isinf(dDD.s.hi)) &&
                         crt_isfinite(aDD.s.hi) && crt_isfinite(bDD.s.hi))
		{
			makeFinite(cDD);
			makeFinite(dDD);
			real.s.hi = crt_copysign(0.0,(aDD.s.hi*cDD.s.hi + bDD.s.hi*dDD.s.hi));
			real.s.lo = 0.0;
			imag.s.hi = crt_copysign(0.0,(bDD.s.hi*cDD.s.hi - aDD.s.hi*dDD.s.hi));
			imag.s.lo = 0.0;
		}
	}
	
	long double _Complex z;
	__real__ z = real.ld;
	__imag__ z = imag.ld;
	
	return z;
}
