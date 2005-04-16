/*
 * Linux/PA-RISC Project (http://www.parisc-linux.org/)
 *
 * Floating-point emulation code
 *  Copyright (C) 2001 Hewlett-Packard (Paul Bame) <bame@debian.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * BEGIN_DESC
 *
 *  Purpose:
 *	Single Floating-point Round to Integer
 *	Double Floating-point Round to Integer
 *	Quad Floating-point Round to Integer (returns unimplemented)
 *
 *  External Interfaces:
 *	dbl_frnd(srcptr,nullptr,dstptr,status)
 *	sgl_frnd(srcptr,nullptr,dstptr,status)
 *
 * END_DESC
*/


#include "float.h"
#include "sgl_float.h"
#include "dbl_float.h"
#include "cnv_float.h"

/*
 *  Single Floating-point Round to Integer
 */

/*ARGSUSED*/
int
sgl_frnd(sgl_floating_point *srcptr,
	unsigned int *nullptr,
	sgl_floating_point *dstptr,
	unsigned int *status)
{
	register unsigned int src, result;
	register int src_exponent;
	register boolean inexact = FALSE;

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
                 * return quiet NaN or infinity
                 */
                *dstptr = src;
                return(NOEXCEPTION);
        }
	/* 
	 * Need to round?
	 */
	if ((src_exponent -= SGL_BIAS) >= SGL_P - 1) {
		*dstptr = src;
		return(NOEXCEPTION);
	}
	/*
	 * Generate result
	 */
	if (src_exponent >= 0) {
		Sgl_clear_exponent_set_hidden(src);
		result = src;
		Sgl_rightshift(result,(SGL_P-1) - (src_exponent));
		/* check for inexact */
		if (Sgl_isinexact_to_fix(src,src_exponent)) {
			inexact = TRUE;
			/*  round result  */
			switch (Rounding_mode()) {
			case ROUNDPLUS:
			     if (Sgl_iszero_sign(src)) Sgl_increment(result);
			     break;
			case ROUNDMINUS:
			     if (Sgl_isone_sign(src)) Sgl_increment(result);
			     break;
			case ROUNDNEAREST:
			     if (Sgl_isone_roundbit(src,src_exponent))
			        if (Sgl_isone_stickybit(src,src_exponent) 
				|| (Sgl_isone_lowmantissa(result))) 
					Sgl_increment(result);
			} 
		}
		Sgl_leftshift(result,(SGL_P-1) - (src_exponent));
		if (Sgl_isone_hiddenoverflow(result)) 
			Sgl_set_exponent(result,src_exponent + (SGL_BIAS+1));
		else Sgl_set_exponent(result,src_exponent + SGL_BIAS);
	}
	else {
		result = src;  		/* set sign */
		Sgl_setzero_exponentmantissa(result);
		/* check for inexact */
		if (Sgl_isnotzero_exponentmantissa(src)) {
			inexact = TRUE;
			/*  round result  */
			switch (Rounding_mode()) {
			case ROUNDPLUS:
			     if (Sgl_iszero_sign(src)) 
				Sgl_set_exponent(result,SGL_BIAS);
			     break;
			case ROUNDMINUS:
			     if (Sgl_isone_sign(src)) 
				Sgl_set_exponent(result,SGL_BIAS);
			     break;
			case ROUNDNEAREST:
			     if (src_exponent == -1)
			        if (Sgl_isnotzero_mantissa(src))
				   Sgl_set_exponent(result,SGL_BIAS);
			} 
		}
	}
	*dstptr = result;
	if (inexact) {
		if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
		else Set_inexactflag();
	}
	return(NOEXCEPTION);
} 

/*
 *  Double Floating-point Round to Integer
 */

/*ARGSUSED*/
int
dbl_frnd(
	dbl_floating_point *srcptr,
	unsigned int *nullptr,
	dbl_floating_point *dstptr,
	unsigned int *status)
{
	register unsigned int srcp1, srcp2, resultp1, resultp2;
	register int src_exponent;
	register boolean inexact = FALSE;

	Dbl_copyfromptr(srcptr,srcp1,srcp2);
        /*
         * check source operand for NaN or infinity
         */
        if ((src_exponent = Dbl_exponent(srcp1)) == DBL_INFINITY_EXPONENT) {
                /*
                 * is signaling NaN?
                 */
                if (Dbl_isone_signaling(srcp1)) {
                        /* trap if INVALIDTRAP enabled */
                        if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                        /* make NaN quiet */
                        Set_invalidflag();
                        Dbl_set_quiet(srcp1);
                }
                /*
                 * return quiet NaN or infinity
                 */
                Dbl_copytoptr(srcp1,srcp2,dstptr);
                return(NOEXCEPTION);
        }
	/* 
	 * Need to round?
	 */
	if ((src_exponent -= DBL_BIAS) >= DBL_P - 1) {
		Dbl_copytoptr(srcp1,srcp2,dstptr);
		return(NOEXCEPTION);
	}
	/*
	 * Generate result
	 */
	if (src_exponent >= 0) {
		Dbl_clear_exponent_set_hidden(srcp1);
		resultp1 = srcp1;
		resultp2 = srcp2;
		Dbl_rightshift(resultp1,resultp2,(DBL_P-1) - (src_exponent));
		/* check for inexact */
		if (Dbl_isinexact_to_fix(srcp1,srcp2,src_exponent)) {
			inexact = TRUE;
			/*  round result  */
			switch (Rounding_mode()) {
			case ROUNDPLUS:
			     if (Dbl_iszero_sign(srcp1)) 
				Dbl_increment(resultp1,resultp2);
			     break;
			case ROUNDMINUS:
			     if (Dbl_isone_sign(srcp1)) 
				Dbl_increment(resultp1,resultp2);
			     break;
			case ROUNDNEAREST:
			     if (Dbl_isone_roundbit(srcp1,srcp2,src_exponent))
			      if (Dbl_isone_stickybit(srcp1,srcp2,src_exponent) 
				  || (Dbl_isone_lowmantissap2(resultp2))) 
					Dbl_increment(resultp1,resultp2);
			} 
		}
		Dbl_leftshift(resultp1,resultp2,(DBL_P-1) - (src_exponent));
		if (Dbl_isone_hiddenoverflow(resultp1))
			Dbl_set_exponent(resultp1,src_exponent + (DBL_BIAS+1));
		else Dbl_set_exponent(resultp1,src_exponent + DBL_BIAS);
	}
	else {
		resultp1 = srcp1;  /* set sign */
		Dbl_setzero_exponentmantissa(resultp1,resultp2);
		/* check for inexact */
		if (Dbl_isnotzero_exponentmantissa(srcp1,srcp2)) {
			inexact = TRUE;
			/*  round result  */
			switch (Rounding_mode()) {
			case ROUNDPLUS:
			     if (Dbl_iszero_sign(srcp1)) 
				Dbl_set_exponent(resultp1,DBL_BIAS);
			     break;
			case ROUNDMINUS:
			     if (Dbl_isone_sign(srcp1)) 
				Dbl_set_exponent(resultp1,DBL_BIAS);
			     break;
			case ROUNDNEAREST:
			     if (src_exponent == -1)
			        if (Dbl_isnotzero_mantissa(srcp1,srcp2))
				   Dbl_set_exponent(resultp1,DBL_BIAS);
			} 
		}
	}
	Dbl_copytoptr(resultp1,resultp2,dstptr);
	if (inexact) {
		if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
		else Set_inexactflag();
	}
	return(NOEXCEPTION);
}
