// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * Floating-point emulation code
 *  Copyright (C) 2001 Hewlett-Packard (Paul Bame) <bame@debian.org>
 */
/*
 * BEGIN_DESC
 *
 *  File:
 *	@(#)	pa/spmath/sfsqrt.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Single Floating-point Square Root
 *
 *  External Interfaces:
 *	sgl_fsqrt(srcptr,nullptr,dstptr,status)
 *
 *  Internal Interfaces:
 *
 *  Theory:
 *	<<please update with a overview of the operation of this file>>
 *
 * END_DESC
*/


#include "float.h"
#include "sgl_float.h"

/*
 *  Single Floating-point Square Root
 */

/*ARGSUSED*/
unsigned int
sgl_fsqrt(
    sgl_floating_point *srcptr,
    unsigned int *nullptr,
    sgl_floating_point *dstptr,
    unsigned int *status)
{
	register unsigned int src, result;
	register int src_exponent;
	register unsigned int newbit, sum;
	register boolean guardbit = FALSE, even_exponent;

	src = *srcptr;
        /*
         * check source operand for NaN or infinity
         */
        if ((src_exponent = Sgl_exponent(src)) == SGL_INFINITY_EXPONENT) {
                /*
                 * is signaling NaN?
                 */
                if (Sgl_isone_signaling(src)) {
                        /* trap if INVALIDTRAP enabled */
                        if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                        /* make NaN quiet */
                        Set_invalidflag();
                        Sgl_set_quiet(src);
                }
                /*
                 * Return quiet NaN or positive infinity.
		 *  Fall through to negative test if negative infinity.
                 */
		if (Sgl_iszero_sign(src) || Sgl_isnotzero_mantissa(src)) {
                	*dstptr = src;
                	return(NOEXCEPTION);
		}
        }

        /*
         * check for zero source operand
         */
	if (Sgl_iszero_exponentmantissa(src)) {
		*dstptr = src;
		return(NOEXCEPTION);
	}

        /*
         * check for negative source operand 
         */
	if (Sgl_isone_sign(src)) {
		/* trap if INVALIDTRAP enabled */
		if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
		/* make NaN quiet */
		Set_invalidflag();
		Sgl_makequietnan(src);
		*dstptr = src;
		return(NOEXCEPTION);
	}

	/*
	 * Generate result
	 */
	if (src_exponent > 0) {
		even_exponent = Sgl_hidden(src);
		Sgl_clear_signexponent_set_hidden(src);
	}
	else {
		/* normalize operand */
		Sgl_clear_signexponent(src);
		src_exponent++;
		Sgl_normalize(src,src_exponent);
		even_exponent = src_exponent & 1;
	}
	if (even_exponent) {
		/* exponent is even */
		/* Add comment here.  Explain why odd exponent needs correction */
		Sgl_leftshiftby1(src);
	}
	/*
	 * Add comment here.  Explain following algorithm.
	 * 
	 * Trust me, it works.
	 *
	 */
	Sgl_setzero(result);
	newbit = 1 << SGL_P;
	while (newbit && Sgl_isnotzero(src)) {
		Sgl_addition(result,newbit,sum);
		if(sum <= Sgl_all(src)) {
			/* update result */
			Sgl_addition(result,(newbit<<1),result);
			Sgl_subtract(src,sum,src);
		}
		Sgl_rightshiftby1(newbit);
		Sgl_leftshiftby1(src);
	}
	/* correct exponent for pre-shift */
	if (even_exponent) {
		Sgl_rightshiftby1(result);
	}

	/* check for inexact */
	if (Sgl_isnotzero(src)) {
		if (!even_exponent && Sgl_islessthan(result,src)) 
			Sgl_increment(result);
		guardbit = Sgl_lowmantissa(result);
		Sgl_rightshiftby1(result);

		/*  now round result  */
		switch (Rounding_mode()) {
		case ROUNDPLUS:
		     Sgl_increment(result);
		     break;
		case ROUNDNEAREST:
		     /* stickybit is always true, so guardbit 
		      * is enough to determine rounding */
		     if (guardbit) {
			Sgl_increment(result);
		     }
		     break;
		}
		/* increment result exponent by 1 if mantissa overflowed */
		if (Sgl_isone_hiddenoverflow(result)) src_exponent+=2;

		if (Is_inexacttrap_enabled()) {
			Sgl_set_exponent(result,
			 ((src_exponent-SGL_BIAS)>>1)+SGL_BIAS);
			*dstptr = result;
			return(INEXACTEXCEPTION);
		}
		else Set_inexactflag();
	}
	else {
		Sgl_rightshiftby1(result);
	}
	Sgl_set_exponent(result,((src_exponent-SGL_BIAS)>>1)+SGL_BIAS);
	*dstptr = result;
	return(NOEXCEPTION);
}
