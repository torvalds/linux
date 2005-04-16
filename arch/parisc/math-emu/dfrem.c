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
 *	@(#)	pa/spmath/dfrem.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Double Precision Floating-point Remainder
 *
 *  External Interfaces:
 *	dbl_frem(srcptr1,srcptr2,dstptr,status)
 *
 *  Internal Interfaces:
 *
 *  Theory:
 *	<<please update with a overview of the operation of this file>>
 *
 * END_DESC
*/



#include "float.h"
#include "dbl_float.h"

/*
 *  Double Precision Floating-point Remainder
 */

int
dbl_frem (dbl_floating_point * srcptr1, dbl_floating_point * srcptr2,
	  dbl_floating_point * dstptr, unsigned int *status)
{
	register unsigned int opnd1p1, opnd1p2, opnd2p1, opnd2p2;
	register unsigned int resultp1, resultp2;
	register int opnd1_exponent, opnd2_exponent, dest_exponent, stepcount;
	register boolean roundup = FALSE;

	Dbl_copyfromptr(srcptr1,opnd1p1,opnd1p2);
	Dbl_copyfromptr(srcptr2,opnd2p1,opnd2p2);
	/*
	 * check first operand for NaN's or infinity
	 */
	if ((opnd1_exponent = Dbl_exponent(opnd1p1)) == DBL_INFINITY_EXPONENT) {
		if (Dbl_iszero_mantissa(opnd1p1,opnd1p2)) {
			if (Dbl_isnotnan(opnd2p1,opnd2p2)) {
				/* invalid since first operand is infinity */
				if (Is_invalidtrap_enabled()) 
                                	return(INVALIDEXCEPTION);
                                Set_invalidflag();
                                Dbl_makequietnan(resultp1,resultp2);
				Dbl_copytoptr(resultp1,resultp2,dstptr);
				return(NOEXCEPTION);
			}
		}
		else {
                	/*
                 	 * is NaN; signaling or quiet?
                 	 */
                	if (Dbl_isone_signaling(opnd1p1)) {
                        	/* trap if INVALIDTRAP enabled */
                        	if (Is_invalidtrap_enabled()) 
                            		return(INVALIDEXCEPTION);
                        	/* make NaN quiet */
                        	Set_invalidflag();
                        	Dbl_set_quiet(opnd1p1);
                	}
			/* 
			 * is second operand a signaling NaN? 
			 */
			else if (Dbl_is_signalingnan(opnd2p1)) {
                        	/* trap if INVALIDTRAP enabled */
                        	if (Is_invalidtrap_enabled()) 
                            		return(INVALIDEXCEPTION);
                        	/* make NaN quiet */
                        	Set_invalidflag();
                        	Dbl_set_quiet(opnd2p1);
				Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
                		return(NOEXCEPTION);
			}
                	/*
                 	 * return quiet NaN
                 	 */
			Dbl_copytoptr(opnd1p1,opnd1p2,dstptr);
                	return(NOEXCEPTION);
		}
	} 
	/*
	 * check second operand for NaN's or infinity
	 */
	if ((opnd2_exponent = Dbl_exponent(opnd2p1)) == DBL_INFINITY_EXPONENT) {
		if (Dbl_iszero_mantissa(opnd2p1,opnd2p2)) {
			/*
			 * return first operand
			 */
			Dbl_copytoptr(opnd1p1,opnd1p2,dstptr);
			return(NOEXCEPTION);
		}
                /*
                 * is NaN; signaling or quiet?
                 */
                if (Dbl_isone_signaling(opnd2p1)) {
                        /* trap if INVALIDTRAP enabled */
                        if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                        /* make NaN quiet */
                        Set_invalidflag();
                        Dbl_set_quiet(opnd2p1);
                }
                /*
                 * return quiet NaN
                 */
		Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
                return(NOEXCEPTION);
	}
	/*
	 * check second operand for zero
	 */
	if (Dbl_iszero_exponentmantissa(opnd2p1,opnd2p2)) {
		/* invalid since second operand is zero */
		if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                Set_invalidflag();
                Dbl_makequietnan(resultp1,resultp2);
		Dbl_copytoptr(resultp1,resultp2,dstptr);
		return(NOEXCEPTION);
	}

	/* 
	 * get sign of result
	 */
	resultp1 = opnd1p1;  

	/* 
	 * check for denormalized operands
	 */
	if (opnd1_exponent == 0) {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd1p1,opnd1p2)) {
			Dbl_copytoptr(opnd1p1,opnd1p2,dstptr);
			return(NOEXCEPTION);
		}
		/* normalize, then continue */
		opnd1_exponent = 1;
		Dbl_normalize(opnd1p1,opnd1p2,opnd1_exponent);
	}
	else {
		Dbl_clear_signexponent_set_hidden(opnd1p1);
	}
	if (opnd2_exponent == 0) {
		/* normalize, then continue */
		opnd2_exponent = 1;
		Dbl_normalize(opnd2p1,opnd2p2,opnd2_exponent);
	}
	else {
		Dbl_clear_signexponent_set_hidden(opnd2p1);
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
		if (stepcount == -1 && 
		    Dbl_isgreaterthan(opnd1p1,opnd1p2,opnd2p1,opnd2p2)) {
			/* set sign */
			Dbl_allp1(resultp1) = ~Dbl_allp1(resultp1);
			/* align opnd2 with opnd1 */
			Dbl_leftshiftby1(opnd2p1,opnd2p2); 
			Dbl_subtract(opnd2p1,opnd2p2,opnd1p1,opnd1p2,
			 opnd2p1,opnd2p2);
			/* now normalize */
                	while (Dbl_iszero_hidden(opnd2p1)) {
                        	Dbl_leftshiftby1(opnd2p1,opnd2p2);
                        	dest_exponent--;
			}
			Dbl_set_exponentmantissa(resultp1,resultp2,opnd2p1,opnd2p2);
			goto testforunderflow;
		}
		/*
		 * opnd1/opnd2 <= 1/2
		 *
		 * In this case n will round to zero, so 
		 *    r = opnd1
		 */
		Dbl_set_exponentmantissa(resultp1,resultp2,opnd1p1,opnd1p2);
		dest_exponent = opnd1_exponent;
		goto testforunderflow;
	}

	/*
	 * Generate result
	 *
	 * Do iterative subtract until remainder is less than operand 2.
	 */
	while (stepcount-- > 0 && (Dbl_allp1(opnd1p1) || Dbl_allp2(opnd1p2))) {
		if (Dbl_isnotlessthan(opnd1p1,opnd1p2,opnd2p1,opnd2p2)) {
			Dbl_subtract(opnd1p1,opnd1p2,opnd2p1,opnd2p2,opnd1p1,opnd1p2);
		}
		Dbl_leftshiftby1(opnd1p1,opnd1p2);
	}
	/*
	 * Do last subtract, then determine which way to round if remainder 
	 * is exactly 1/2 of opnd2 
	 */
	if (Dbl_isnotlessthan(opnd1p1,opnd1p2,opnd2p1,opnd2p2)) {
		Dbl_subtract(opnd1p1,opnd1p2,opnd2p1,opnd2p2,opnd1p1,opnd1p2);
		roundup = TRUE;
	}
	if (stepcount > 0 || Dbl_iszero(opnd1p1,opnd1p2)) {
		/* division is exact, remainder is zero */
		Dbl_setzero_exponentmantissa(resultp1,resultp2);
		Dbl_copytoptr(resultp1,resultp2,dstptr);
		return(NOEXCEPTION);
	}

	/* 
	 * Check for cases where opnd1/opnd2 < n 
	 *
	 * In this case the result's sign will be opposite that of
	 * opnd1.  The mantissa also needs some correction.
	 */
	Dbl_leftshiftby1(opnd1p1,opnd1p2);
	if (Dbl_isgreaterthan(opnd1p1,opnd1p2,opnd2p1,opnd2p2)) {
		Dbl_invert_sign(resultp1);
		Dbl_leftshiftby1(opnd2p1,opnd2p2);
		Dbl_subtract(opnd2p1,opnd2p2,opnd1p1,opnd1p2,opnd1p1,opnd1p2);
	}
	/* check for remainder being exactly 1/2 of opnd2 */
	else if (Dbl_isequal(opnd1p1,opnd1p2,opnd2p1,opnd2p2) && roundup) { 
		Dbl_invert_sign(resultp1);
	}

	/* normalize result's mantissa */
        while (Dbl_iszero_hidden(opnd1p1)) {
                dest_exponent--;
                Dbl_leftshiftby1(opnd1p1,opnd1p2);
        }
	Dbl_set_exponentmantissa(resultp1,resultp2,opnd1p1,opnd1p2);

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
                        Dbl_setwrapped_exponent(resultp1,dest_exponent,unfl);
			/* frem is always exact */
			Dbl_copytoptr(resultp1,resultp2,dstptr);
			return(UNDERFLOWEXCEPTION);
                }
                /*
                 * denormalize result or set to signed zero
                 */
                if (dest_exponent >= (1 - DBL_P)) {
			Dbl_rightshift_exponentmantissa(resultp1,resultp2,
			 1-dest_exponent);
                }
                else {
			Dbl_setzero_exponentmantissa(resultp1,resultp2);
		}
	}
	else Dbl_set_exponent(resultp1,dest_exponent);
	Dbl_copytoptr(resultp1,resultp2,dstptr);
	return(NOEXCEPTION);
}
