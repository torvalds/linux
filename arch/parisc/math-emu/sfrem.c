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
 *	@(#)	pa/spmath/sfrem.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Single Precision Floating-point Remainder
 *
 *  External Interfaces:
 *	sgl_frem(srcptr1,srcptr2,dstptr,status)
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
 *  Single Precision Floating-point Remainder
 */

int
sgl_frem (sgl_floating_point * srcptr1, sgl_floating_point * srcptr2,
	  sgl_floating_point * dstptr, unsigned int *status)
{
	register unsigned int opnd1, opnd2, result;
	register int opnd1_exponent, opnd2_exponent, dest_exponent, stepcount;
	register boolean roundup = FALSE;

	opnd1 = *srcptr1;
	opnd2 = *srcptr2;
	/*
	 * check first operand for NaN's or infinity
	 */
	if ((opnd1_exponent = Sgl_exponent(opnd1)) == SGL_INFINITY_EXPONENT) {
		if (Sgl_iszero_mantissa(opnd1)) {
			if (Sgl_isnotnan(opnd2)) {
				/* invalid since first operand is infinity */
				if (Is_invalidtrap_enabled()) 
                                	return(INVALIDEXCEPTION);
                                Set_invalidflag();
                                Sgl_makequietnan(result);
				*dstptr = result;
				return(NOEXCEPTION);
			}
		}
		else {
                	/*
                 	 * is NaN; signaling or quiet?
                 	 */
                	if (Sgl_isone_signaling(opnd1)) {
                        	/* trap if INVALIDTRAP enabled */
                        	if (Is_invalidtrap_enabled()) 
                            		return(INVALIDEXCEPTION);
                        	/* make NaN quiet */
                        	Set_invalidflag();
                        	Sgl_set_quiet(opnd1);
                	}
			/* 
			 * is second operand a signaling NaN? 
			 */
			else if (Sgl_is_signalingnan(opnd2)) {
                        	/* trap if INVALIDTRAP enabled */
                        	if (Is_invalidtrap_enabled()) 
                            		return(INVALIDEXCEPTION);
                        	/* make NaN quiet */
                        	Set_invalidflag();
                        	Sgl_set_quiet(opnd2);
                		*dstptr = opnd2;
                		return(NOEXCEPTION);
			}
                	/*
                 	 * return quiet NaN
                 	 */
                	*dstptr = opnd1;
                	return(NOEXCEPTION);
		}
	} 
	/*
	 * check second operand for NaN's or infinity
	 */
	if ((opnd2_exponent = Sgl_exponent(opnd2)) == SGL_INFINITY_EXPONENT) {
		if (Sgl_iszero_mantissa(opnd2)) {
			/*
			 * return first operand
			 */
                	*dstptr = opnd1;
			return(NOEXCEPTION);
		}
                /*
                 * is NaN; signaling or quiet?
                 */
                if (Sgl_isone_signaling(opnd2)) {
                        /* trap if INVALIDTRAP enabled */
                        if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                        /* make NaN quiet */
                        Set_invalidflag();
                        Sgl_set_quiet(opnd2);
                }
                /*
                 * return quiet NaN
                 */
                *dstptr = opnd2;
                return(NOEXCEPTION);
	}
	/*
	 * check second operand for zero
	 */
	if (Sgl_iszero_exponentmantissa(opnd2)) {
		/* invalid since second operand is zero */
		if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                Set_invalidflag();
                Sgl_makequietnan(result);
		*dstptr = result;
		return(NOEXCEPTION);
	}

	/* 
	 * get sign of result
	 */
	result = opnd1;  

	/* 
	 * check for denormalized operands
	 */
	if (opnd1_exponent == 0) {
		/* check for zero */
		if (Sgl_iszero_mantissa(opnd1)) {
			*dstptr = opnd1;
			return(NOEXCEPTION);
		}
		/* normalize, then continue */
		opnd1_exponent = 1;
		Sgl_normalize(opnd1,opnd1_exponent);
	}
	else {
		Sgl_clear_signexponent_set_hidden(opnd1);
	}
	if (opnd2_exponent == 0) {
		/* normalize, then continue */
		opnd2_exponent = 1;
		Sgl_normalize(opnd2,opnd2_exponent);
	}
	else {
		Sgl_clear_signexponent_set_hidden(opnd2);
	}

	/* find result exponent and divide step loop count */
	dest_exponent = opnd2_exponent - 1;
	stepcount = opnd1_exponent - opnd2_exponent;

	/*
	 * check for opnd1/opnd2 < 1
	 */
	if (stepcount < 0) {
		/*
		 * check for opnd1/opnd2 > 1/2
		 *
		 * In this case n will round to 1, so 
		 *    r = opnd1 - opnd2 
		 */
		if (stepcount == -1 && Sgl_isgreaterthan(opnd1,opnd2)) {
			Sgl_all(result) = ~Sgl_all(result);   /* set sign */
			/* align opnd2 with opnd1 */
			Sgl_leftshiftby1(opnd2); 
			Sgl_subtract(opnd2,opnd1,opnd2);
			/* now normalize */
                	while (Sgl_iszero_hidden(opnd2)) {
                        	Sgl_leftshiftby1(opnd2);
                        	dest_exponent--;
			}
			Sgl_set_exponentmantissa(result,opnd2);
			goto testforunderflow;
		}
		/*
		 * opnd1/opnd2 <= 1/2
		 *
		 * In this case n will round to zero, so 
		 *    r = opnd1
		 */
		Sgl_set_exponentmantissa(result,opnd1);
		dest_exponent = opnd1_exponent;
		goto testforunderflow;
	}

	/*
	 * Generate result
	 *
	 * Do iterative subtract until remainder is less than operand 2.
	 */
	while (stepcount-- > 0 && Sgl_all(opnd1)) {
		if (Sgl_isnotlessthan(opnd1,opnd2))
			Sgl_subtract(opnd1,opnd2,opnd1);
		Sgl_leftshiftby1(opnd1);
	}
	/*
	 * Do last subtract, then determine which way to round if remainder 
	 * is exactly 1/2 of opnd2 
	 */
	if (Sgl_isnotlessthan(opnd1,opnd2)) {
		Sgl_subtract(opnd1,opnd2,opnd1);
		roundup = TRUE;
	}
	if (stepcount > 0 || Sgl_iszero(opnd1)) {
		/* division is exact, remainder is zero */
		Sgl_setzero_exponentmantissa(result);
		*dstptr = result;
		return(NOEXCEPTION);
	}

	/* 
	 * Check for cases where opnd1/opnd2 < n 
	 *
	 * In this case the result's sign will be opposite that of
	 * opnd1.  The mantissa also needs some correction.
	 */
	Sgl_leftshiftby1(opnd1);
	if (Sgl_isgreaterthan(opnd1,opnd2)) {
		Sgl_invert_sign(result);
		Sgl_subtract((opnd2<<1),opnd1,opnd1);
	}
	/* check for remainder being exactly 1/2 of opnd2 */
	else if (Sgl_isequal(opnd1,opnd2) && roundup) { 
		Sgl_invert_sign(result);
	}

	/* normalize result's mantissa */
        while (Sgl_iszero_hidden(opnd1)) {
                dest_exponent--;
                Sgl_leftshiftby1(opnd1);
        }
	Sgl_set_exponentmantissa(result,opnd1);

        /* 
         * Test for underflow
         */
    testforunderflow:
	if (dest_exponent <= 0) {
                /* trap if UNDERFLOWTRAP enabled */
                if (Is_underflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                        Sgl_setwrapped_exponent(result,dest_exponent,unfl);
			*dstptr = result;
			/* frem is always exact */
			return(UNDERFLOWEXCEPTION);
                }
                /*
                 * denormalize result or set to signed zero
                 */
                if (dest_exponent >= (1 - SGL_P)) {
			Sgl_rightshift_exponentmantissa(result,1-dest_exponent);
                }
                else {
			Sgl_setzero_exponentmantissa(result);
		}
	}
	else Sgl_set_exponent(result,dest_exponent);
	*dstptr = result;
	return(NOEXCEPTION);
}
