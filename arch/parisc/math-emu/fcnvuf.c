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
 *  File:
 *	@(#)	pa/spmath/fcnvuf.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Fixed point to Floating-point Converts
 *
 *  External Interfaces:
 *	dbl_to_dbl_fcnvuf(srcptr,nullptr,dstptr,status)
 *	dbl_to_sgl_fcnvuf(srcptr,nullptr,dstptr,status)
 *	sgl_to_dbl_fcnvuf(srcptr,nullptr,dstptr,status)
 *	sgl_to_sgl_fcnvuf(srcptr,nullptr,dstptr,status)
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
#include "dbl_float.h"
#include "cnv_float.h"

/************************************************************************
 *  Fixed point to Floating-point Converts				*
 ************************************************************************/

/*
 *  Convert Single Unsigned Fixed to Single Floating-point format
 */

int
sgl_to_sgl_fcnvuf(
			unsigned int *srcptr,
			unsigned int *nullptr,
			sgl_floating_point *dstptr,
			unsigned int *status)
{
	register unsigned int src, result = 0;
	register int dst_exponent;

	src = *srcptr;

	/* Check for zero */ 
	if (src == 0) { 
	       	Sgl_setzero(result); 
		*dstptr = result;
	       	return(NOEXCEPTION); 
	} 
	/*
	 * Generate exponent and normalized mantissa
	 */
	dst_exponent = 16;    /* initialize for normalization */
	/*
	 * Check word for most significant bit set.  Returns
	 * a value in dst_exponent indicating the bit position,
	 * between -1 and 30.
	 */
	Find_ms_one_bit(src,dst_exponent);
	/*  left justify source, with msb at bit position 0  */
	src <<= dst_exponent+1;
	Sgl_set_mantissa(result, src >> SGL_EXP_LENGTH);
	Sgl_set_exponent(result, 30+SGL_BIAS - dst_exponent);

	/* check for inexact */
	if (Suint_isinexact_to_sgl(src)) {
		switch (Rounding_mode()) {
			case ROUNDPLUS: 
				Sgl_increment(result);
				break;
			case ROUNDMINUS: /* never negative */
				break;
			case ROUNDNEAREST:
				Sgl_roundnearest_from_suint(src,result);
				break;
		}
		if (Is_inexacttrap_enabled()) {
			*dstptr = result;
			return(INEXACTEXCEPTION);
		}
		else Set_inexactflag();
	}
	*dstptr = result;
	return(NOEXCEPTION);
}

/*
 *  Single Unsigned Fixed to Double Floating-point 
 */

int
sgl_to_dbl_fcnvuf(
			unsigned int *srcptr,
			unsigned int *nullptr,
			dbl_floating_point *dstptr,
			unsigned int *status)
{
	register int dst_exponent;
	register unsigned int src, resultp1 = 0, resultp2 = 0;

	src = *srcptr;

	/* Check for zero */
	if (src == 0) {
	       	Dbl_setzero(resultp1,resultp2);
	       	Dbl_copytoptr(resultp1,resultp2,dstptr);
	       	return(NOEXCEPTION);
	}
	/*
	 * Generate exponent and normalized mantissa
	 */
	dst_exponent = 16;    /* initialize for normalization */
	/*
	 * Check word for most significant bit set.  Returns
	 * a value in dst_exponent indicating the bit position,
	 * between -1 and 30.
	 */
	Find_ms_one_bit(src,dst_exponent);
	/*  left justify source, with msb at bit position 0  */
	src <<= dst_exponent+1;
	Dbl_set_mantissap1(resultp1, src >> DBL_EXP_LENGTH);
	Dbl_set_mantissap2(resultp2, src << (32-DBL_EXP_LENGTH));
	Dbl_set_exponent(resultp1, (30+DBL_BIAS) - dst_exponent);
	Dbl_copytoptr(resultp1,resultp2,dstptr);
	return(NOEXCEPTION);
}

/*
 *  Double Unsigned Fixed to Single Floating-point 
 */

int
dbl_to_sgl_fcnvuf(
			dbl_unsigned *srcptr,
			unsigned int *nullptr,
			sgl_floating_point *dstptr,
			unsigned int *status)
{
	int dst_exponent;
	unsigned int srcp1, srcp2, result = 0;

	Duint_copyfromptr(srcptr,srcp1,srcp2);

	/* Check for zero */
	if (srcp1 == 0 && srcp2 == 0) {
	       	Sgl_setzero(result);
	       	*dstptr = result;
	       	return(NOEXCEPTION);
	}
	/*
	 * Generate exponent and normalized mantissa
	 */
	dst_exponent = 16;    /* initialize for normalization */
	if (srcp1 == 0) {
		/*
		 * Check word for most significant bit set.  Returns
		 * a value in dst_exponent indicating the bit position,
		 * between -1 and 30.
		 */
		Find_ms_one_bit(srcp2,dst_exponent);
		/*  left justify source, with msb at bit position 0  */
		srcp1 = srcp2 << dst_exponent+1;    
		srcp2 = 0;
		/*
		 *  since msb set is in second word, need to 
		 *  adjust bit position count
		 */
		dst_exponent += 32;
	}
	else {
		/*
		 * Check word for most significant bit set.  Returns
		 * a value in dst_exponent indicating the bit position,
		 * between -1 and 30.
		 *
		 */
		Find_ms_one_bit(srcp1,dst_exponent);
		/*  left justify source, with msb at bit position 0  */
		if (dst_exponent >= 0) {
			Variable_shift_double(srcp1,srcp2,(31-dst_exponent),
			 srcp1); 
			srcp2 <<= dst_exponent+1;
		}
	}
	Sgl_set_mantissa(result, srcp1 >> SGL_EXP_LENGTH);
	Sgl_set_exponent(result, (62+SGL_BIAS) - dst_exponent);

	/* check for inexact */
	if (Duint_isinexact_to_sgl(srcp1,srcp2)) {
		switch (Rounding_mode()) {
			case ROUNDPLUS: 
				Sgl_increment(result);
				break;
			case ROUNDMINUS: /* never negative */
				break;
			case ROUNDNEAREST:
				Sgl_roundnearest_from_duint(srcp1,srcp2,result);
				break;
		}
		if (Is_inexacttrap_enabled()) {
			*dstptr = result;
			return(INEXACTEXCEPTION);
		}
		else Set_inexactflag();
	}
	*dstptr = result;
	return(NOEXCEPTION);
}

/*
 *  Double Unsigned Fixed to Double Floating-point 
 */

int
dbl_to_dbl_fcnvuf(
		    dbl_unsigned *srcptr,
		    unsigned int *nullptr,
		    dbl_floating_point *dstptr,
		    unsigned int *status)
{
	register int dst_exponent;
	register unsigned int srcp1, srcp2, resultp1 = 0, resultp2 = 0;

	Duint_copyfromptr(srcptr,srcp1,srcp2);

	/* Check for zero */
	if (srcp1 == 0 && srcp2 ==0) {
	       	Dbl_setzero(resultp1,resultp2);
	       	Dbl_copytoptr(resultp1,resultp2,dstptr);
	       	return(NOEXCEPTION);
	}
	/*
	 * Generate exponent and normalized mantissa
	 */
	dst_exponent = 16;    /* initialize for normalization */
	if (srcp1 == 0) {
		/*
		 * Check word for most significant bit set.  Returns
		 * a value in dst_exponent indicating the bit position,
		 * between -1 and 30.
		 */
		Find_ms_one_bit(srcp2,dst_exponent);
		/*  left justify source, with msb at bit position 0  */
		srcp1 = srcp2 << dst_exponent+1;
		srcp2 = 0;
		/*
		 *  since msb set is in second word, need to 
		 *  adjust bit position count
		 */
		dst_exponent += 32;
	}
	else {
		/*
		 * Check word for most significant bit set.  Returns
		 * a value in dst_exponent indicating the bit position,
		 * between -1 and 30.
		 */
		Find_ms_one_bit(srcp1,dst_exponent);
		/*  left justify source, with msb at bit position 0  */
		if (dst_exponent >= 0) {
			Variable_shift_double(srcp1,srcp2,(31-dst_exponent),
			 srcp1); 
			srcp2 <<= dst_exponent+1;
		}
	}
	Dbl_set_mantissap1(resultp1, srcp1 >> DBL_EXP_LENGTH);
	Shiftdouble(srcp1,srcp2,DBL_EXP_LENGTH,resultp2);
	Dbl_set_exponent(resultp1, (62+DBL_BIAS) - dst_exponent);

	/* check for inexact */
	if (Duint_isinexact_to_dbl(srcp2)) {
		switch (Rounding_mode()) {
			case ROUNDPLUS: 
				Dbl_increment(resultp1,resultp2);
				break;
			case ROUNDMINUS: /* never negative */
				break;
			case ROUNDNEAREST:
				Dbl_roundnearest_from_duint(srcp2,resultp1,
				resultp2);
				break;
		}
		if (Is_inexacttrap_enabled()) {
			Dbl_copytoptr(resultp1,resultp2,dstptr);
			return(INEXACTEXCEPTION);
		}
		else Set_inexactflag();
	}
	Dbl_copytoptr(resultp1,resultp2,dstptr);
	return(NOEXCEPTION);
}

