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
 *	@(#)	pa/spmath/dfmpy.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Double Precision Floating-point Multiply
 *
 *  External Interfaces:
 *	dbl_fmpy(srcptr1,srcptr2,dstptr,status)
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
 *  Double Precision Floating-point Multiply
 */

int
dbl_fmpy(
	    dbl_floating_point *srcptr1,
	    dbl_floating_point *srcptr2,
	    dbl_floating_point *dstptr,
	    unsigned int *status)
{
	register unsigned int opnd1p1, opnd1p2, opnd2p1, opnd2p2;
	register unsigned int opnd3p1, opnd3p2, resultp1, resultp2;
	register int dest_exponent, count;
	register boolean inexact = FALSE, guardbit = FALSE, stickybit = FALSE;
	boolean is_tiny;

	Dbl_copyfromptr(srcptr1,opnd1p1,opnd1p2);
	Dbl_copyfromptr(srcptr2,opnd2p1,opnd2p2);

	/* 
	 * set sign bit of result 
	 */
	if (Dbl_sign(opnd1p1) ^ Dbl_sign(opnd2p1)) 
		Dbl_setnegativezerop1(resultp1); 
	else Dbl_setzerop1(resultp1);
	/*
	 * check first operand for NaN's or infinity
	 */
	if (Dbl_isinfinity_exponent(opnd1p1)) {
		if (Dbl_iszero_mantissa(opnd1p1,opnd1p2)) {
			if (Dbl_isnotnan(opnd2p1,opnd2p2)) {
				if (Dbl_iszero_exponentmantissa(opnd2p1,opnd2p2)) {
					/* 
					 * invalid since operands are infinity 
					 * and zero 
					 */
					if (Is_invalidtrap_enabled())
                                		return(INVALIDEXCEPTION);
                                	Set_invalidflag();
                                	Dbl_makequietnan(resultp1,resultp2);
					Dbl_copytoptr(resultp1,resultp2,dstptr);
					return(NOEXCEPTION);
				}
				/*
			 	 * return infinity
			 	 */
				Dbl_setinfinity_exponentmantissa(resultp1,resultp2);
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
	if (Dbl_isinfinity_exponent(opnd2p1)) {
		if (Dbl_iszero_mantissa(opnd2p1,opnd2p2)) {
			if (Dbl_iszero_exponentmantissa(opnd1p1,opnd1p2)) {
				/* invalid since operands are zero & infinity */
				if (Is_invalidtrap_enabled())
                                	return(INVALIDEXCEPTION);
                                Set_invalidflag();
                                Dbl_makequietnan(opnd2p1,opnd2p2);
				Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
				return(NOEXCEPTION);
			}
			/*
			 * return infinity
			 */
			Dbl_setinfinity_exponentmantissa(resultp1,resultp2);
			Dbl_copytoptr(resultp1,resultp2,dstptr);
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
	 * Generate exponent 
	 */
	dest_exponent = Dbl_exponent(opnd1p1) + Dbl_exponent(opnd2p1) -DBL_BIAS;

	/*
	 * Generate mantissa
	 */
	if (Dbl_isnotzero_exponent(opnd1p1)) {
		/* set hidden bit */
		Dbl_clear_signexponent_set_hidden(opnd1p1);
	}
	else {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd1p1,opnd1p2)) {
			Dbl_setzero_exponentmantissa(resultp1,resultp2);
			Dbl_copytoptr(resultp1,resultp2,dstptr);
			return(NOEXCEPTION);
		}
                /* is denormalized, adjust exponent */
                Dbl_clear_signexponent(opnd1p1);
                Dbl_leftshiftby1(opnd1p1,opnd1p2);
		Dbl_normalize(opnd1p1,opnd1p2,dest_exponent);
	}
	/* opnd2 needs to have hidden bit set with msb in hidden bit */
	if (Dbl_isnotzero_exponent(opnd2p1)) {
		Dbl_clear_signexponent_set_hidden(opnd2p1);
	}
	else {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd2p1,opnd2p2)) {
			Dbl_setzero_exponentmantissa(resultp1,resultp2);
			Dbl_copytoptr(resultp1,resultp2,dstptr);
			return(NOEXCEPTION);
		}
                /* is denormalized; want to normalize */
                Dbl_clear_signexponent(opnd2p1);
                Dbl_leftshiftby1(opnd2p1,opnd2p2);
		Dbl_normalize(opnd2p1,opnd2p2,dest_exponent);
	}

	/* Multiply two source mantissas together */

	/* make room for guard bits */
	Dbl_leftshiftby7(opnd2p1,opnd2p2);
	Dbl_setzero(opnd3p1,opnd3p2);
        /* 
         * Four bits at a time are inspected in each loop, and a 
         * simple shift and add multiply algorithm is used. 
         */ 
	for (count=1;count<=DBL_P;count+=4) {
		stickybit |= Dlow4p2(opnd3p2);
		Dbl_rightshiftby4(opnd3p1,opnd3p2);
		if (Dbit28p2(opnd1p2)) {
	 		/* Twoword_add should be an ADDC followed by an ADD. */
                        Twoword_add(opnd3p1, opnd3p2, opnd2p1<<3 | opnd2p2>>29, 
				    opnd2p2<<3);
		}
		if (Dbit29p2(opnd1p2)) {
                        Twoword_add(opnd3p1, opnd3p2, opnd2p1<<2 | opnd2p2>>30, 
				    opnd2p2<<2);
		}
		if (Dbit30p2(opnd1p2)) {
                        Twoword_add(opnd3p1, opnd3p2, opnd2p1<<1 | opnd2p2>>31,
				    opnd2p2<<1);
		}
		if (Dbit31p2(opnd1p2)) {
                        Twoword_add(opnd3p1, opnd3p2, opnd2p1, opnd2p2);
		}
		Dbl_rightshiftby4(opnd1p1,opnd1p2);
	}
	if (Dbit3p1(opnd3p1)==0) {
		Dbl_leftshiftby1(opnd3p1,opnd3p2);
	}
	else {
		/* result mantissa >= 2. */
		dest_exponent++;
	}
	/* check for denormalized result */
	while (Dbit3p1(opnd3p1)==0) {
		Dbl_leftshiftby1(opnd3p1,opnd3p2);
		dest_exponent--;
	}
	/*
	 * check for guard, sticky and inexact bits 
	 */
	stickybit |= Dallp2(opnd3p2) << 25;
	guardbit = (Dallp2(opnd3p2) << 24) >> 31;
	inexact = guardbit | stickybit;

	/* align result mantissa */
	Dbl_rightshiftby8(opnd3p1,opnd3p2);

	/* 
	 * round result 
	 */
	if (inexact && (dest_exponent>0 || Is_underflowtrap_enabled())) {
		Dbl_clear_signexponent(opnd3p1);
		switch (Rounding_mode()) {
			case ROUNDPLUS: 
				if (Dbl_iszero_sign(resultp1)) 
					Dbl_increment(opnd3p1,opnd3p2);
				break;
			case ROUNDMINUS: 
				if (Dbl_isone_sign(resultp1)) 
					Dbl_increment(opnd3p1,opnd3p2);
				break;
			case ROUNDNEAREST:
				if (guardbit) {
			   	if (stickybit || Dbl_isone_lowmantissap2(opnd3p2))
			      	Dbl_increment(opnd3p1,opnd3p2);
				}
		}
		if (Dbl_isone_hidden(opnd3p1)) dest_exponent++;
	}
	Dbl_set_mantissa(resultp1,resultp2,opnd3p1,opnd3p2);

        /* 
         * Test for overflow
         */
	if (dest_exponent >= DBL_INFINITY_EXPONENT) {
                /* trap if OVERFLOWTRAP enabled */
                if (Is_overflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
			Dbl_setwrapped_exponent(resultp1,dest_exponent,ovfl);
			Dbl_copytoptr(resultp1,resultp2,dstptr);
			if (inexact) 
			    if (Is_inexacttrap_enabled())
				return (OVERFLOWEXCEPTION | INEXACTEXCEPTION);
			    else Set_inexactflag();
			return (OVERFLOWEXCEPTION);
                }
		inexact = TRUE;
		Set_overflowflag();
                /* set result to infinity or largest number */
		Dbl_setoverflow(resultp1,resultp2);
	}
        /* 
         * Test for underflow
         */
	else if (dest_exponent <= 0) {
                /* trap if UNDERFLOWTRAP enabled */
                if (Is_underflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
			Dbl_setwrapped_exponent(resultp1,dest_exponent,unfl);
			Dbl_copytoptr(resultp1,resultp2,dstptr);
			if (inexact) 
			    if (Is_inexacttrap_enabled())
				return (UNDERFLOWEXCEPTION | INEXACTEXCEPTION);
			    else Set_inexactflag();
			return (UNDERFLOWEXCEPTION);
                }

		/* Determine if should set underflow flag */
		is_tiny = TRUE;
		if (dest_exponent == 0 && inexact) {
			switch (Rounding_mode()) {
			case ROUNDPLUS: 
				if (Dbl_iszero_sign(resultp1)) {
					Dbl_increment(opnd3p1,opnd3p2);
					if (Dbl_isone_hiddenoverflow(opnd3p1))
                			    is_tiny = FALSE;
					Dbl_decrement(opnd3p1,opnd3p2);
				}
				break;
			case ROUNDMINUS: 
				if (Dbl_isone_sign(resultp1)) {
					Dbl_increment(opnd3p1,opnd3p2);
					if (Dbl_isone_hiddenoverflow(opnd3p1))
                			    is_tiny = FALSE;
					Dbl_decrement(opnd3p1,opnd3p2);
				}
				break;
			case ROUNDNEAREST:
				if (guardbit && (stickybit || 
				    Dbl_isone_lowmantissap2(opnd3p2))) {
				      	Dbl_increment(opnd3p1,opnd3p2);
					if (Dbl_isone_hiddenoverflow(opnd3p1))
                			    is_tiny = FALSE;
					Dbl_decrement(opnd3p1,opnd3p2);
				}
				break;
			}
		}

		/*
		 * denormalize result or set to signed zero
		 */
		stickybit = inexact;
		Dbl_denormalize(opnd3p1,opnd3p2,dest_exponent,guardbit,
		 stickybit,inexact);

		/* return zero or smallest number */
		if (inexact) {
			switch (Rounding_mode()) {
			case ROUNDPLUS: 
				if (Dbl_iszero_sign(resultp1)) {
					Dbl_increment(opnd3p1,opnd3p2);
				}
				break;
			case ROUNDMINUS: 
				if (Dbl_isone_sign(resultp1)) {
					Dbl_increment(opnd3p1,opnd3p2);
				}
				break;
			case ROUNDNEAREST:
				if (guardbit && (stickybit || 
				    Dbl_isone_lowmantissap2(opnd3p2))) {
			      		Dbl_increment(opnd3p1,opnd3p2);
				}
				break;
			}
                	if (is_tiny) Set_underflowflag();
		}
		Dbl_set_exponentmantissa(resultp1,resultp2,opnd3p1,opnd3p2);
	}
	else Dbl_set_exponent(resultp1,dest_exponent);
	/* check for inexact */
	Dbl_copytoptr(resultp1,resultp2,dstptr);
	if (inexact) {
		if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
		else Set_inexactflag();
	}
	return(NOEXCEPTION);
}
