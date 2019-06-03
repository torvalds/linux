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
 *	@(#)	pa/spmath/dfsub.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Double_subtract: subtract two double precision values.
 *
 *  External Interfaces:
 *	dbl_fsub(leftptr, rightptr, dstptr, status)
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
 * Double_subtract: subtract two double precision values.
 */
int
dbl_fsub(
	    dbl_floating_point *leftptr,
	    dbl_floating_point *rightptr,
	    dbl_floating_point *dstptr,
	    unsigned int *status)
    {
    register unsigned int signless_upper_left, signless_upper_right, save;
    register unsigned int leftp1, leftp2, rightp1, rightp2, extent;
    register unsigned int resultp1 = 0, resultp2 = 0;
    
    register int result_exponent, right_exponent, diff_exponent;
    register int sign_save, jumpsize;
    register boolean inexact = FALSE, underflowtrap;
        
    /* Create local copies of the numbers */
    Dbl_copyfromptr(leftptr,leftp1,leftp2);
    Dbl_copyfromptr(rightptr,rightp1,rightp2);

    /* A zero "save" helps discover equal operands (for later),  *
     * and is used in swapping operands (if needed).             */
    Dbl_xortointp1(leftp1,rightp1,/*to*/save);

    /*
     * check first operand for NaN's or infinity
     */
    if ((result_exponent = Dbl_exponent(leftp1)) == DBL_INFINITY_EXPONENT)
	{
	if (Dbl_iszero_mantissa(leftp1,leftp2)) 
	    {
	    if (Dbl_isnotnan(rightp1,rightp2)) 
		{
		if (Dbl_isinfinity(rightp1,rightp2) && save==0) 
		    {
		    /* 
		     * invalid since operands are same signed infinity's
		     */
		    if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                    Set_invalidflag();
                    Dbl_makequietnan(resultp1,resultp2);
		    Dbl_copytoptr(resultp1,resultp2,dstptr);
		    return(NOEXCEPTION);
		    }
		/*
	 	 * return infinity
	 	 */
		Dbl_copytoptr(leftp1,leftp2,dstptr);
		return(NOEXCEPTION);
		}
	    }
	else 
	    {
            /*
             * is NaN; signaling or quiet?
             */
            if (Dbl_isone_signaling(leftp1)) 
		{
               	/* trap if INVALIDTRAP enabled */
		if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
        	/* make NaN quiet */
        	Set_invalidflag();
        	Dbl_set_quiet(leftp1);
        	}
	    /* 
	     * is second operand a signaling NaN? 
	     */
	    else if (Dbl_is_signalingnan(rightp1)) 
		{
        	/* trap if INVALIDTRAP enabled */
               	if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
		/* make NaN quiet */
		Set_invalidflag();
		Dbl_set_quiet(rightp1);
		Dbl_copytoptr(rightp1,rightp2,dstptr);
		return(NOEXCEPTION);
		}
	    /*
 	     * return quiet NaN
 	     */
	    Dbl_copytoptr(leftp1,leftp2,dstptr);
 	    return(NOEXCEPTION);
	    }
	} /* End left NaN or Infinity processing */
    /*
     * check second operand for NaN's or infinity
     */
    if (Dbl_isinfinity_exponent(rightp1)) 
	{
	if (Dbl_iszero_mantissa(rightp1,rightp2)) 
	    {
	    /* return infinity */
	    Dbl_invert_sign(rightp1);
	    Dbl_copytoptr(rightp1,rightp2,dstptr);
	    return(NOEXCEPTION);
	    }
        /*
         * is NaN; signaling or quiet?
         */
        if (Dbl_isone_signaling(rightp1)) 
	    {
            /* trap if INVALIDTRAP enabled */
	    if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
	    /* make NaN quiet */
	    Set_invalidflag();
	    Dbl_set_quiet(rightp1);
	    }
	/*
	 * return quiet NaN
 	 */
	Dbl_copytoptr(rightp1,rightp2,dstptr);
	return(NOEXCEPTION);
    	} /* End right NaN or Infinity processing */

    /* Invariant: Must be dealing with finite numbers */

    /* Compare operands by removing the sign */
    Dbl_copytoint_exponentmantissap1(leftp1,signless_upper_left);
    Dbl_copytoint_exponentmantissap1(rightp1,signless_upper_right);

    /* sign difference selects add or sub operation. */
    if(Dbl_ismagnitudeless(leftp2,rightp2,signless_upper_left,signless_upper_right))
	{
	/* Set the left operand to the larger one by XOR swap *
	 *  First finish the first word using "save"          */
	Dbl_xorfromintp1(save,rightp1,/*to*/rightp1);
	Dbl_xorfromintp1(save,leftp1,/*to*/leftp1);
     	Dbl_swap_lower(leftp2,rightp2);
	result_exponent = Dbl_exponent(leftp1);
	Dbl_invert_sign(leftp1);
	}
    /* Invariant:  left is not smaller than right. */ 

    if((right_exponent = Dbl_exponent(rightp1)) == 0)
        {
	/* Denormalized operands.  First look for zeroes */
	if(Dbl_iszero_mantissa(rightp1,rightp2)) 
	    {
	    /* right is zero */
	    if(Dbl_iszero_exponentmantissa(leftp1,leftp2))
		{
		/* Both operands are zeros */
		Dbl_invert_sign(rightp1);
		if(Is_rounding_mode(ROUNDMINUS))
		    {
		    Dbl_or_signs(leftp1,/*with*/rightp1);
		    }
		else
		    {
		    Dbl_and_signs(leftp1,/*with*/rightp1);
		    }
		}
	    else 
		{
		/* Left is not a zero and must be the result.  Trapped
		 * underflows are signaled if left is denormalized.  Result
		 * is always exact. */
		if( (result_exponent == 0) && Is_underflowtrap_enabled() )
		    {
		    /* need to normalize results mantissa */
	    	    sign_save = Dbl_signextendedsign(leftp1);
		    Dbl_leftshiftby1(leftp1,leftp2);
		    Dbl_normalize(leftp1,leftp2,result_exponent);
		    Dbl_set_sign(leftp1,/*using*/sign_save);
                    Dbl_setwrapped_exponent(leftp1,result_exponent,unfl);
		    Dbl_copytoptr(leftp1,leftp2,dstptr);
		    /* inexact = FALSE */
		    return(UNDERFLOWEXCEPTION);
		    }
		}
	    Dbl_copytoptr(leftp1,leftp2,dstptr);
	    return(NOEXCEPTION);
	    }

	/* Neither are zeroes */
	Dbl_clear_sign(rightp1);	/* Exponent is already cleared */
	if(result_exponent == 0 )
	    {
	    /* Both operands are denormalized.  The result must be exact
	     * and is simply calculated.  A sum could become normalized and a
	     * difference could cancel to a true zero. */
	    if( (/*signed*/int) save >= 0 )
		{
		Dbl_subtract(leftp1,leftp2,/*minus*/rightp1,rightp2,
		 /*into*/resultp1,resultp2);
		if(Dbl_iszero_mantissa(resultp1,resultp2))
		    {
		    if(Is_rounding_mode(ROUNDMINUS))
			{
			Dbl_setone_sign(resultp1);
			}
		    else
			{
			Dbl_setzero_sign(resultp1);
			}
		    Dbl_copytoptr(resultp1,resultp2,dstptr);
		    return(NOEXCEPTION);
		    }
		}
	    else
		{
		Dbl_addition(leftp1,leftp2,rightp1,rightp2,
		 /*into*/resultp1,resultp2);
		if(Dbl_isone_hidden(resultp1))
		    {
		    Dbl_copytoptr(resultp1,resultp2,dstptr);
		    return(NOEXCEPTION);
		    }
		}
	    if(Is_underflowtrap_enabled())
		{
		/* need to normalize result */
	    	sign_save = Dbl_signextendedsign(resultp1);
		Dbl_leftshiftby1(resultp1,resultp2);
		Dbl_normalize(resultp1,resultp2,result_exponent);
		Dbl_set_sign(resultp1,/*using*/sign_save);
                Dbl_setwrapped_exponent(resultp1,result_exponent,unfl);
		Dbl_copytoptr(resultp1,resultp2,dstptr);
		/* inexact = FALSE */
		return(UNDERFLOWEXCEPTION);
		}
	    Dbl_copytoptr(resultp1,resultp2,dstptr);
	    return(NOEXCEPTION);
	    }
	right_exponent = 1;	/* Set exponent to reflect different bias
				 * with denomalized numbers. */
	}
    else
	{
	Dbl_clear_signexponent_set_hidden(rightp1);
	}
    Dbl_clear_exponent_set_hidden(leftp1);
    diff_exponent = result_exponent - right_exponent;

    /* 
     * Special case alignment of operands that would force alignment 
     * beyond the extent of the extension.  A further optimization
     * could special case this but only reduces the path length for this
     * infrequent case.
     */
    if(diff_exponent > DBL_THRESHOLD)
	{
	diff_exponent = DBL_THRESHOLD;
	}
    
    /* Align right operand by shifting to right */
    Dbl_right_align(/*operand*/rightp1,rightp2,/*shifted by*/diff_exponent,
     /*and lower to*/extent);

    /* Treat sum and difference of the operands separately. */
    if( (/*signed*/int) save >= 0 )
	{
	/*
	 * Difference of the two operands.  Their can be no overflow.  A
	 * borrow can occur out of the hidden bit and force a post
	 * normalization phase.
	 */
	Dbl_subtract_withextension(leftp1,leftp2,/*minus*/rightp1,rightp2,
	 /*with*/extent,/*into*/resultp1,resultp2);
	if(Dbl_iszero_hidden(resultp1))
	    {
	    /* Handle normalization */
	    /* A straight forward algorithm would now shift the result
	     * and extension left until the hidden bit becomes one.  Not
	     * all of the extension bits need participate in the shift.
	     * Only the two most significant bits (round and guard) are
	     * needed.  If only a single shift is needed then the guard
	     * bit becomes a significant low order bit and the extension
	     * must participate in the rounding.  If more than a single 
	     * shift is needed, then all bits to the right of the guard 
	     * bit are zeros, and the guard bit may or may not be zero. */
	    sign_save = Dbl_signextendedsign(resultp1);
            Dbl_leftshiftby1_withextent(resultp1,resultp2,extent,resultp1,resultp2);

            /* Need to check for a zero result.  The sign and exponent
	     * fields have already been zeroed.  The more efficient test
	     * of the full object can be used.
	     */
    	    if(Dbl_iszero(resultp1,resultp2))
		/* Must have been "x-x" or "x+(-x)". */
		{
		if(Is_rounding_mode(ROUNDMINUS)) Dbl_setone_sign(resultp1);
		Dbl_copytoptr(resultp1,resultp2,dstptr);
		return(NOEXCEPTION);
		}
	    result_exponent--;
	    /* Look to see if normalization is finished. */
	    if(Dbl_isone_hidden(resultp1))
		{
		if(result_exponent==0)
		    {
		    /* Denormalized, exponent should be zero.  Left operand *
		     * was normalized, so extent (guard, round) was zero    */
		    goto underflow;
		    }
		else
		    {
		    /* No further normalization is needed. */
		    Dbl_set_sign(resultp1,/*using*/sign_save);
	    	    Ext_leftshiftby1(extent);
		    goto round;
		    }
		}

	    /* Check for denormalized, exponent should be zero.  Left    *
	     * operand was normalized, so extent (guard, round) was zero */
	    if(!(underflowtrap = Is_underflowtrap_enabled()) &&
	       result_exponent==0) goto underflow;

	    /* Shift extension to complete one bit of normalization and
	     * update exponent. */
	    Ext_leftshiftby1(extent);

	    /* Discover first one bit to determine shift amount.  Use a
	     * modified binary search.  We have already shifted the result
	     * one position right and still not found a one so the remainder
	     * of the extension must be zero and simplifies rounding. */
	    /* Scan bytes */
	    while(Dbl_iszero_hiddenhigh7mantissa(resultp1))
		{
		Dbl_leftshiftby8(resultp1,resultp2);
		if((result_exponent -= 8) <= 0  && !underflowtrap)
		    goto underflow;
		}
	    /* Now narrow it down to the nibble */
	    if(Dbl_iszero_hiddenhigh3mantissa(resultp1))
		{
		/* The lower nibble contains the normalizing one */
		Dbl_leftshiftby4(resultp1,resultp2);
		if((result_exponent -= 4) <= 0 && !underflowtrap)
		    goto underflow;
		}
	    /* Select case were first bit is set (already normalized)
	     * otherwise select the proper shift. */
	    if((jumpsize = Dbl_hiddenhigh3mantissa(resultp1)) > 7)
		{
		/* Already normalized */
		if(result_exponent <= 0) goto underflow;
		Dbl_set_sign(resultp1,/*using*/sign_save);
		Dbl_set_exponent(resultp1,/*using*/result_exponent);
		Dbl_copytoptr(resultp1,resultp2,dstptr);
		return(NOEXCEPTION);
		}
	    Dbl_sethigh4bits(resultp1,/*using*/sign_save);
	    switch(jumpsize) 
		{
		case 1:
		    {
		    Dbl_leftshiftby3(resultp1,resultp2);
		    result_exponent -= 3;
		    break;
		    }
		case 2:
		case 3:
		    {
		    Dbl_leftshiftby2(resultp1,resultp2);
		    result_exponent -= 2;
		    break;
		    }
		case 4:
		case 5:
		case 6:
		case 7:
		    {
		    Dbl_leftshiftby1(resultp1,resultp2);
		    result_exponent -= 1;
		    break;
		    }
		}
	    if(result_exponent > 0) 
		{
		Dbl_set_exponent(resultp1,/*using*/result_exponent);
		Dbl_copytoptr(resultp1,resultp2,dstptr);
		return(NOEXCEPTION);		/* Sign bit is already set */
		}
	    /* Fixup potential underflows */
	  underflow:
	    if(Is_underflowtrap_enabled())
		{
		Dbl_set_sign(resultp1,sign_save);
                Dbl_setwrapped_exponent(resultp1,result_exponent,unfl);
		Dbl_copytoptr(resultp1,resultp2,dstptr);
		/* inexact = FALSE */
		return(UNDERFLOWEXCEPTION);
		}
	    /* 
	     * Since we cannot get an inexact denormalized result,
	     * we can now return.
	     */
	    Dbl_fix_overshift(resultp1,resultp2,(1-result_exponent),extent);
	    Dbl_clear_signexponent(resultp1);
	    Dbl_set_sign(resultp1,sign_save);
	    Dbl_copytoptr(resultp1,resultp2,dstptr);
	    return(NOEXCEPTION);
	    } /* end if(hidden...)... */
	/* Fall through and round */
	} /* end if(save >= 0)... */
    else 
	{
	/* Subtract magnitudes */
	Dbl_addition(leftp1,leftp2,rightp1,rightp2,/*to*/resultp1,resultp2);
	if(Dbl_isone_hiddenoverflow(resultp1))
	    {
	    /* Prenormalization required. */
	    Dbl_rightshiftby1_withextent(resultp2,extent,extent);
	    Dbl_arithrightshiftby1(resultp1,resultp2);
	    result_exponent++;
	    } /* end if hiddenoverflow... */
	} /* end else ...subtract magnitudes... */
    
    /* Round the result.  If the extension is all zeros,then the result is
     * exact.  Otherwise round in the correct direction.  No underflow is
     * possible. If a postnormalization is necessary, then the mantissa is
     * all zeros so no shift is needed. */
  round:
    if(Ext_isnotzero(extent))
	{
	inexact = TRUE;
	switch(Rounding_mode())
	    {
	    case ROUNDNEAREST: /* The default. */
	    if(Ext_isone_sign(extent))
		{
		/* at least 1/2 ulp */
		if(Ext_isnotzero_lower(extent)  ||
		  Dbl_isone_lowmantissap2(resultp2))
		    {
		    /* either exactly half way and odd or more than 1/2ulp */
		    Dbl_increment(resultp1,resultp2);
		    }
		}
	    break;

	    case ROUNDPLUS:
	    if(Dbl_iszero_sign(resultp1))
		{
		/* Round up positive results */
		Dbl_increment(resultp1,resultp2);
		}
	    break;
	    
	    case ROUNDMINUS:
	    if(Dbl_isone_sign(resultp1))
		{
		/* Round down negative results */
		Dbl_increment(resultp1,resultp2);
		}
	    
	    case ROUNDZERO:;
	    /* truncate is simple */
	    } /* end switch... */
	if(Dbl_isone_hiddenoverflow(resultp1)) result_exponent++;
	}
    if(result_exponent == DBL_INFINITY_EXPONENT)
        {
        /* Overflow */
        if(Is_overflowtrap_enabled())
	    {
	    Dbl_setwrapped_exponent(resultp1,result_exponent,ovfl);
	    Dbl_copytoptr(resultp1,resultp2,dstptr);
	    if (inexact)
	    if (Is_inexacttrap_enabled())
		return(OVERFLOWEXCEPTION | INEXACTEXCEPTION);
		else Set_inexactflag();
	    return(OVERFLOWEXCEPTION);
	    }
        else
	    {
	    inexact = TRUE;
	    Set_overflowflag();
	    Dbl_setoverflow(resultp1,resultp2);
	    }
	}
    else Dbl_set_exponent(resultp1,result_exponent);
    Dbl_copytoptr(resultp1,resultp2,dstptr);
    if(inexact) 
	if(Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
	else Set_inexactflag();
    return(NOEXCEPTION);
    }
