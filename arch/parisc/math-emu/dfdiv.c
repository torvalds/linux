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
 *	@(#)	pa/spmath/dfdiv.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Double Precision Floating-point Divide
 *
 *  External Interfaces:
 *	dbl_fdiv(srcptr1,srcptr2,dstptr,status)
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
 *  Double Precision Floating-point Divide
 */

int
dbl_fdiv (dbl_floating_point * srcptr1, dbl_floating_point * srcptr2,
	  dbl_floating_point * dstptr, unsigned int *status)
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
				if (Dbl_isinfinity(opnd2p1,opnd2p2)) {
					/* 
					 * invalid since both operands 
					 * are infinity 
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
			/*
			 * return zero
			 */
			Dbl_setzero_exponentmantissa(resultp1,resultp2);
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
         * check for division by zero
         */
        if (Dbl_iszero_exponentmantissa(opnd2p1,opnd2p2)) {
                if (Dbl_iszero_exponentmantissa(opnd1p1,opnd1p2)) {
                        /* invalid since both operands are zero */
                        if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                        Set_invalidflag();
                        Dbl_makequietnan(resultp1,resultp2);
                        Dbl_copytoptr(resultp1,resultp2,dstptr);
                        return(NOEXCEPTION);
                }
                if (Is_divisionbyzerotrap_enabled())
                       	return(DIVISIONBYZEROEXCEPTION);
                Set_divisionbyzeroflag();
                Dbl_setinfinity_exponentmantissa(resultp1,resultp2);
                Dbl_copytoptr(resultp1,resultp2,dstptr);
                return(NOEXCEPTION);
        }
	/*
	 * Generate exponent 
	 */
	dest_exponent = Dbl_exponent(opnd1p1) - Dbl_exponent(opnd2p1) + DBL_BIAS;

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
                /* is denormalized, want to normalize */
                Dbl_clear_signexponent(opnd1p1);
                Dbl_leftshiftby1(opnd1p1,opnd1p2);
		Dbl_normalize(opnd1p1,opnd1p2,dest_exponent);
	}
	/* opnd2 needs to have hidden bit set with msb in hidden bit */
	if (Dbl_isnotzero_exponent(opnd2p1)) {
		Dbl_clear_signexponent_set_hidden(opnd2p1);
	}
	else {
                /* is denormalized; want to normalize */
                Dbl_clear_signexponent(opnd2p1);
                Dbl_leftshiftby1(opnd2p1,opnd2p2);
                while (Dbl_iszero_hiddenhigh7mantissa(opnd2p1)) {
                        dest_exponent+=8;
                        Dbl_leftshiftby8(opnd2p1,opnd2p2);
                }
                if (Dbl_iszero_hiddenhigh3mantissa(opnd2p1)) {
                        dest_exponent+=4;
                        Dbl_leftshiftby4(opnd2p1,opnd2p2);
                }
                while (Dbl_iszero_hidden(opnd2p1)) {
                        dest_exponent++;
                        Dbl_leftshiftby1(opnd2p1,opnd2p2);
                }
	}

	/* Divide the source mantissas */

	/* 
	 * A non-restoring divide algorithm is used.
	 */
	Twoword_subtract(opnd1p1,opnd1p2,opnd2p1,opnd2p2);
	Dbl_setzero(opnd3p1,opnd3p2);
	for (count=1; count <= DBL_P && (opnd1p1 || opnd1p2); count++) {
		Dbl_leftshiftby1(opnd1p1,opnd1p2);
		Dbl_leftshiftby1(opnd3p1,opnd3p2);
		if (Dbl_iszero_sign(opnd1p1)) {
			Dbl_setone_lowmantissap2(opnd3p2);
			Twoword_subtract(opnd1p1,opnd1p2,opnd2p1,opnd2p2);
		}
		else {
			Twoword_add(opnd1p1, opnd1p2, opnd2p1, opnd2p2);
		}
	}
	if (count <= DBL_P) {
		Dbl_leftshiftby1(opnd3p1,opnd3p2);
		Dbl_setone_lowmantissap2(opnd3p2);
		Dbl_leftshift(opnd3p1,opnd3p2,(DBL_P-count));
		if (Dbl_iszero_hidden(opnd3p1)) {
			Dbl_leftshiftby1(opnd3p1,opnd3p2);
			dest_exponent--;
		}
	}
	else {
		if (Dbl_iszero_hidden(opnd3p1)) {
			/* need to get one more bit of result */
			Dbl_leftshiftby1(opnd1p1,opnd1p2);
			Dbl_leftshiftby1(opnd3p1,opnd3p2);
			if (Dbl_iszero_sign(opnd1p1)) {
				Dbl_setone_lowmantissap2(opnd3p2);
				Twoword_subtract(opnd1p1,opnd1p2,opnd2p1,opnd2p2);
			}
			else {
				Twoword_add(opnd1p1,opnd1p2,opnd2p1,opnd2p2);
			}
			dest_exponent--;
		}
		if (Dbl_iszero_sign(opnd1p1)) guardbit = TRUE;
		stickybit = Dbl_allp1(opnd1p1) || Dbl_allp2(opnd1p2);
	}
	inexact = guardbit | stickybit;

	/* 
	 * round result 
	 */
	if (inexact && (dest_exponent > 0 || Is_underflowtrap_enabled())) {
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
				if (guardbit && (stickybit || 
				    Dbl_isone_lowmantissap2(opnd3p2))) {
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
                                return(OVERFLOWEXCEPTION | INEXACTEXCEPTION);
                            else Set_inexactflag();
                        return(OVERFLOWEXCEPTION);
                }
		Set_overflowflag();
                /* set result to infinity or largest number */
		Dbl_setoverflow(resultp1,resultp2);
		inexact = TRUE;
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
                                return(UNDERFLOWEXCEPTION | INEXACTEXCEPTION);
                            else Set_inexactflag();
                        return(UNDERFLOWEXCEPTION);
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

		/* return rounded number */ 
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
	Dbl_copytoptr(resultp1,resultp2,dstptr);

	/* check for inexact */
	if (inexact) {
		if (Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
		else Set_inexactflag();
	}
	return(NOEXCEPTION);
}
