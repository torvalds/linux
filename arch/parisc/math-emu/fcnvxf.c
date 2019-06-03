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
 *	@(#)	pa/spmath/fcnvxf.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Single Fixed-point to Single Floating-point
 *	Single Fixed-point to Double Floating-point 
 *	Double Fixed-point to Single Floating-point 
 *	Double Fixed-point to Double Floating-point 
 *
 *  External Interfaces:
 *	dbl_to_dbl_fcnvxf(srcptr,nullptr,dstptr,status)
 *	dbl_to_sgl_fcnvxf(srcptr,nullptr,dstptr,status)
 *	sgl_to_dbl_fcnvxf(srcptr,nullptr,dstptr,status)
 *	sgl_to_sgl_fcnvxf(srcptr,nullptr,dstptr,status)
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

/*
 *  Convert single fixed-point to single floating-point format
 */

int
sgl_to_sgl_fcnvxf(
		    int *srcptr,
		    unsigned int *nullptr,
		    sgl_floating_point *dstptr,
		    unsigned int *status)
{
	register int src, dst_exponent;
	register unsigned int result = 0;

	src = *srcptr;
	/* 
	 * set sign bit of result and get magnitude of source 
	 */
	if (src < 0) {
		Sgl_setone_sign(result);  
		Int_negate(src);
	}
	else {
		Sgl_setzero_sign(result);
        	/* Check for zero */ 
        	if (src == 0) { 
                	Sgl_setzero(result); 
			*dstptr = result;
                	return(NOEXCEPTION); 
        	} 
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
	/*  left justify source, with msb at bit position 1  */
	if (dst_exponent >= 0) src <<= dst_exponent;
	else src = 1 << 30;
	Sgl_set_mantissa(result, src >> (SGL_EXP_LENGTH-1));
	Sgl_set_exponent(result, 30+SGL_BIAS - dst_exponent);

	/* check for inexact */
	if (Int_isinexact_to_sgl(src)) {
		switch (Rounding_mode()) {
			case ROUNDPLUS: 
				if (Sgl_iszero_sign(result)) 
					Sgl_increment(result);
				break;
			case ROUNDMINUS: 
				if (Sgl_isone_sign(result)) 
					Sgl_increment(result);
				break;
			case ROUNDNEAREST:
				Sgl_roundnearest_from_int(src,result);
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
 *  Single Fixed-point to Double Floating-point 
 */

int
sgl_to_dbl_fcnvxf(
		    int *srcptr,
		    unsigned int *nullptr,
		    dbl_floating_point *dstptr,
		    unsigned int *status)
{
	register int src, dst_exponent;
	register unsigned int resultp1 = 0, resultp2 = 0;

	src = *srcptr;
	/* 
	 * set sign bit of result and get magnitude of source 
	 */
	if (src < 0) {
		Dbl_setone_sign(resultp1);  
		Int_negate(src);
	}
	else {
		Dbl_setzero_sign(resultp1);
        	/* Check for zero */
        	if (src == 0) {
                	Dbl_setzero(resultp1,resultp2);
                	Dbl_copytoptr(resultp1,resultp2,dstptr);
                	return(NOEXCEPTION);
        	}
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
	/*  left justify source, with msb at bit position 1  */
	if (dst_exponent >= 0) src <<= dst_exponent;
	else src = 1 << 30;
	Dbl_set_mantissap1(resultp1, src >> DBL_EXP_LENGTH - 1);
	Dbl_set_mantissap2(resultp2, src << (33-DBL_EXP_LENGTH));
	Dbl_set_exponent(resultp1, (30+DBL_BIAS) - dst_exponent);
	Dbl_copytoptr(resultp1,resultp2,dstptr);
	return(NOEXCEPTION);
}

/*
 *  Double Fixed-point to Single Floating-point 
 */

int
dbl_to_sgl_fcnvxf(
			dbl_integer *srcptr,
			unsigned int *nullptr,
			sgl_floating_point *dstptr,
			unsigned int *status)
{
	int dst_exponent, srcp1;
	unsigned int result = 0, srcp2;

	Dint_copyfromptr(srcptr,srcp1,srcp2);
	/* 
	 * set sign bit of result and get magnitude of source 
	 */
	if (srcp1 < 0) {
		Sgl_setone_sign(result);  
		Dint_negate(srcp1,srcp2);
	}
	else {
		Sgl_setzero_sign(result);
        	/* Check for zero */
        	if (srcp1 == 0 && srcp2 == 0) {
                	Sgl_setzero(result);
                	*dstptr = result;
                	return(NOEXCEPTION);
		}
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
		/*  left justify source, with msb at bit position 1  */
		if (dst_exponent >= 0) {
			srcp1 = srcp2 << dst_exponent;    
			srcp2 = 0;
		}
		else {
			srcp1 = srcp2 >> 1;
			srcp2 <<= 31; 
		}
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
		/*  left justify source, with msb at bit position 1  */
		if (dst_exponent > 0) {
			Variable_shift_double(srcp1,srcp2,(32-dst_exponent),
			 srcp1); 
			srcp2 <<= dst_exponent;
		}
		/*
		 * If dst_exponent = 0, we don't need to shift anything.
		 * If dst_exponent = -1, src = - 2**63 so we won't need to 
		 * shift srcp2.
		 */
		else srcp1 >>= -(dst_exponent);
	}
	Sgl_set_mantissa(result, srcp1 >> SGL_EXP_LENGTH - 1);
	Sgl_set_exponent(result, (62+SGL_BIAS) - dst_exponent);

	/* check for inexact */
	if (Dint_isinexact_to_sgl(srcp1,srcp2)) {
		switch (Rounding_mode()) {
			case ROUNDPLUS: 
				if (Sgl_iszero_sign(result)) 
					Sgl_increment(result);
				break;
			case ROUNDMINUS: 
				if (Sgl_isone_sign(result)) 
					Sgl_increment(result);
				break;
			case ROUNDNEAREST:
				Sgl_roundnearest_from_dint(srcp1,srcp2,result);
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
 *  Double Fixed-point to Double Floating-point 
 */

int
dbl_to_dbl_fcnvxf(
		    dbl_integer *srcptr,
		    unsigned int *nullptr,
		    dbl_floating_point *dstptr,
		    unsigned int *status)
{
	register int srcp1, dst_exponent;
	register unsigned int srcp2, resultp1 = 0, resultp2 = 0;

	Dint_copyfromptr(srcptr,srcp1,srcp2);
	/* 
	 * set sign bit of result and get magnitude of source 
	 */
	if (srcp1 < 0) {
		Dbl_setone_sign(resultp1);
		Dint_negate(srcp1,srcp2);
	}
	else {
		Dbl_setzero_sign(resultp1);
        	/* Check for zero */
        	if (srcp1 == 0 && srcp2 ==0) {
                	Dbl_setzero(resultp1,resultp2);
                	Dbl_copytoptr(resultp1,resultp2,dstptr);
                	return(NOEXCEPTION);
		}
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
		/*  left justify source, with msb at bit position 1  */
		if (dst_exponent >= 0) {
			srcp1 = srcp2 << dst_exponent;    
			srcp2 = 0;
		}
		else {
			srcp1 = srcp2 >> 1;
			srcp2 <<= 31;
		}
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
		/*  left justify source, with msb at bit position 1  */
		if (dst_exponent > 0) {
			Variable_shift_double(srcp1,srcp2,(32-dst_exponent),
			 srcp1); 
			srcp2 <<= dst_exponent;
		}
		/*
		 * If dst_exponent = 0, we don't need to shift anything.
		 * If dst_exponent = -1, src = - 2**63 so we won't need to 
		 * shift srcp2.
		 */
		else srcp1 >>= -(dst_exponent);
	}
	Dbl_set_mantissap1(resultp1, srcp1 >> (DBL_EXP_LENGTH-1));
	Shiftdouble(srcp1,srcp2,DBL_EXP_LENGTH-1,resultp2);
	Dbl_set_exponent(resultp1, (62+DBL_BIAS) - dst_exponent);

	/* check for inexact */
	if (Dint_isinexact_to_dbl(srcp2)) {
		switch (Rounding_mode()) {
			case ROUNDPLUS: 
				if (Dbl_iszero_sign(resultp1)) {
					Dbl_increment(resultp1,resultp2);
				}
				break;
			case ROUNDMINUS: 
				if (Dbl_isone_sign(resultp1)) {
					Dbl_increment(resultp1,resultp2);
				}
				break;
			case ROUNDNEAREST:
				Dbl_roundnearest_from_dint(srcp2,resultp1,
				resultp2);
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
