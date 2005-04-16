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
 *	@(#)	pa/spmath/sfsub.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Single_subtract: subtract two single precision values.
 *
 *  External Interfaces:
 *	sgl_fsub(leftptr, rightptr, dstptr, status)
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
 * Single_subtract: subtract two single precision values.
 */
int
sgl_fsub(
	    sgl_floating_point *leftptr,
	    sgl_floating_point *rightptr,
	    sgl_floating_point *dstptr,
	    unsigned int *status)
    {
    register unsigned int left, right, result, extent;
    register unsigned int signless_upper_left, signless_upper_right, save;
    
    register int result_exponent, right_exponent, diff_exponent;
    register int sign_save, jumpsize;
    register boolean inexact = FALSE, underflowtrap;
        
    /* Create local copies of the numbers */
    left = *leftptr;
    right = *rightptr;

    /* A zero "save" helps discover equal operands (for later),  *
     * and is used in swapping operands (if needed).             */
    Sgl_xortointp1(left,right,/*to*/save);

    /*
     * check first operand for NaN's or infinity
     */
    if ((result_exponent = Sgl_exponent(left)) == SGL_INFINITY_EXPONENT)
	{
	if (Sgl_iszero_mantissa(left)) 
	    {
	    if (Sgl_isnotnan(right)) 
		{
		if (Sgl_isinfinity(right) && save==0) 
		    {
		    /* 
		     * invalid since operands are same signed infinity's
		     */
		    if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
                    Set_invalidflag();
                    Sgl_makequietnan(result);
		    *dstptr = result;
		    return(NOEXCEPTION);
		    }
		/*
	 	 * return infinity
	 	 */
		*dstptr = left;
		return(NOEXCEPTION);
		}
	    }
	else 
	    {
            /*
             * is NaN; signaling or quiet?
             */
            if (Sgl_isone_signaling(left)) 
		{
               	/* trap if INVALIDTRAP enabled */
		if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
        	/* make NaN quiet */
        	Set_invalidflag();
        	Sgl_set_quiet(left);
        	}
	    /* 
	     * is second operand a signaling NaN? 
	     */
	    else if (Sgl_is_signalingnan(right)) 
		{
        	/* trap if INVALIDTRAP enabled */
               	if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
		/* make NaN quiet */
		Set_invalidflag();
		Sgl_set_quiet(right);
		*dstptr = right;
		return(NOEXCEPTION);
		}
	    /*
 	     * return quiet NaN
 	     */
 	    *dstptr = left;
 	    return(NOEXCEPTION);
	    }
	} /* End left NaN or Infinity processing */
    /*
     * check second operand for NaN's or infinity
     */
    if (Sgl_isinfinity_exponent(right)) 
	{
	if (Sgl_iszero_mantissa(right)) 
	    {
	    /* return infinity */
	    Sgl_invert_sign(right);
	    *dstptr = right;
	    return(NOEXCEPTION);
	    }
        /*
         * is NaN; signaling or quiet?
         */
        if (Sgl_isone_signaling(right)) 
	    {
            /* trap if INVALIDTRAP enabled */
	    if (Is_invalidtrap_enabled()) return(INVALIDEXCEPTION);
	    /* make NaN quiet */
	    Set_invalidflag();
	    Sgl_set_quiet(right);
	    }
	/*
	 * return quiet NaN
 	 */
	*dstptr = right;
	return(NOEXCEPTION);
    	} /* End right NaN or Infinity processing */

    /* Invariant: Must be dealing with finite numbers */

    /* Compare operands by removing the sign */
    Sgl_copytoint_exponentmantissa(left,signless_upper_left);
    Sgl_copytoint_exponentmantissa(right,signless_upper_right);

    /* sign difference selects sub or add operation. */
    if(Sgl_ismagnitudeless(signless_upper_left,signless_upper_right))
	{
	/* Set the left operand to the larger one by XOR swap *
	 *  First finish the first word using "save"          */
	Sgl_xorfromintp1(save,right,/*to*/right);
	Sgl_xorfromintp1(save,left,/*to*/left);
	result_exponent = Sgl_exponent(left);
	Sgl_invert_sign(left);
	}
    /* Invariant:  left is not smaller than right. */ 

    if((right_exponent = Sgl_exponent(right)) == 0)
        {
	/* Denormalized operands.  First look for zeroes */
	if(Sgl_iszero_mantissa(right)) 
	    {
	    /* right is zero */
	    if(Sgl_iszero_exponentmantissa(left))
		{
		/* Both operands are zeros */
		Sgl_invert_sign(right);
		if(Is_rounding_mode(ROUNDMINUS))
		    {
		    Sgl_or_signs(left,/*with*/right);
		    }
		else
		    {
		    Sgl_and_signs(left,/*with*/right);
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
	    	    sign_save = Sgl_signextendedsign(left);
		    Sgl_leftshiftby1(left);
		    Sgl_normalize(left,result_exponent);
		    Sgl_set_sign(left,/*using*/sign_save);
                    Sgl_setwrapped_exponent(left,result_exponent,unfl);
		    *dstptr = left;
		    /* inexact = FALSE */
		    return(UNDERFLOWEXCEPTION);
		    }
		}
	    *dstptr = left;
	    return(NOEXCEPTION);
	    }

	/* Neither are zeroes */
	Sgl_clear_sign(right);	/* Exponent is already cleared */
	if(result_exponent == 0 )
	    {
	    /* Both operands are denormalized.  The result must be exact
	     * and is simply calculated.  A sum could become normalized and a
	     * difference could cancel to a true zero. */
	    if( (/*signed*/int) save >= 0 )
		{
		Sgl_subtract(left,/*minus*/right,/*into*/result);
		if(Sgl_iszero_mantissa(result))
		    {
		    if(Is_rounding_mode(ROUNDMINUS))
			{
			Sgl_setone_sign(result);
			}
		    else
			{
			Sgl_setzero_sign(result);
			}
		    *dstptr = result;
		    return(NOEXCEPTION);
		    }
		}
	    else
		{
		Sgl_addition(left,right,/*into*/result);
		if(Sgl_isone_hidden(result))
		    {
		    *dstptr = result;
		    return(NOEXCEPTION);
		    }
		}
	    if(Is_underflowtrap_enabled())
		{
		/* need to normalize result */
	    	sign_save = Sgl_signextendedsign(result);
		Sgl_leftshiftby1(result);
		Sgl_normalize(result,result_exponent);
		Sgl_set_sign(result,/*using*/sign_save);
                Sgl_setwrapped_exponent(result,result_exponent,unfl);
		*dstptr = result;
		/* inexact = FALSE */
		return(UNDERFLOWEXCEPTION);
		}
	    *dstptr = result;
	    return(NOEXCEPTION);
	    }
	right_exponent = 1;	/* Set exponent to reflect different bias
				 * with denomalized numbers. */
	}
    else
	{
	Sgl_clear_signexponent_set_hidden(right);
	}
    Sgl_clear_exponent_set_hidden(left);
    diff_exponent = result_exponent - right_exponent;

    /* 
     * Special case alignment of operands that would force alignment 
     * beyond the extent of the extension.  A further optimization
     * could special case this but only reduces the path length for this
     * infrequent case.
     */
    if(diff_exponent > SGL_THRESHOLD)
	{
	diff_exponent = SGL_THRESHOLD;
	}
    
    /* Align right operand by shifting to right */
    Sgl_right_align(/*operand*/right,/*shifted by*/diff_exponent,
      /*and lower to*/extent);

    /* Treat sum and difference of the operands separately. */
    if( (/*signed*/int) save >= 0 )
	{
	/*
	 * Difference of the two operands.  Their can be no overflow.  A
	 * borrow can occur out of the hidden bit and force a post
	 * normalization phase.
	 */
	Sgl_subtract_withextension(left,/*minus*/right,/*with*/extent,/*into*/result);
	if(Sgl_iszero_hidden(result))
	    {
	    /* Handle normalization */
	    /* A straight foward algorithm would now shift the result
	     * and extension left until the hidden bit becomes one.  Not
	     * all of the extension bits need participate in the shift.
	     * Only the two most significant bits (round and guard) are
	     * needed.  If only a single shift is needed then the guard
	     * bit becomes a significant low order bit and the extension
	     * must participate in the rounding.  If more than a single 
	     * shift is needed, then all bits to the right of the guard 
	     * bit are zeros, and the guard bit may or may not be zero. */
	    sign_save = Sgl_signextendedsign(result);
            Sgl_leftshiftby1_withextent(result,extent,result);

            /* Need to check for a zero result.  The sign and exponent
	     * fields have already been zeroed.  The more efficient test
	     * of the full object can be used.
	     */
    	    if(Sgl_iszero(result))
		/* Must have been "x-x" or "x+(-x)". */
		{
		if(Is_rounding_mode(ROUNDMINUS)) Sgl_setone_sign(result);
		*dstptr = result;
		return(NOEXCEPTION);
		}
	    result_exponent--;
	    /* Look to see if normalization is finished. */
	    if(Sgl_isone_hidden(result))
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
		    Sgl_set_sign(result,/*using*/sign_save);
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
	    while(Sgl_iszero_hiddenhigh7mantissa(result))
		{
		Sgl_leftshiftby8(result);
		if((result_exponent -= 8) <= 0  && !underflowtrap)
		    goto underflow;
		}
	    /* Now narrow it down to the nibble */
	    if(Sgl_iszero_hiddenhigh3mantissa(result))
		{
		/* The lower nibble contains the normalizing one */
		Sgl_leftshiftby4(result);
		if((result_exponent -= 4) <= 0 && !underflowtrap)
		    goto underflow;
		}
	    /* Select case were first bit is set (already normalized)
	     * otherwise select the proper shift. */
	    if((jumpsize = Sgl_hiddenhigh3mantissa(result)) > 7)
		{
		/* Already normalized */
		if(result_exponent <= 0) goto underflow;
		Sgl_set_sign(result,/*using*/sign_save);
		Sgl_set_exponent(result,/*using*/result_exponent);
		*dstptr = result;
		return(NOEXCEPTION);
		}
	    Sgl_sethigh4bits(result,/*using*/sign_save);
	    switch(jumpsize) 
		{
		case 1:
		    {
		    Sgl_leftshiftby3(result);
		    result_exponent -= 3;
		    break;
		    }
		case 2:
		case 3:
		    {
		    Sgl_leftshiftby2(result);
		    result_exponent -= 2;
		    break;
		    }
		case 4:
		case 5:
		case 6:
		case 7:
		    {
		    Sgl_leftshiftby1(result);
		    result_exponent -= 1;
		    break;
		    }
		}
	    if(result_exponent > 0) 
		{
		Sgl_set_exponent(result,/*using*/result_exponent);
		*dstptr = result;	/* Sign bit is already set */
		return(NOEXCEPTION);
		}
	    /* Fixup potential underflows */
	  underflow:
	    if(Is_underflowtrap_enabled())
		{
		Sgl_set_sign(result,sign_save);
                Sgl_setwrapped_exponent(result,result_exponent,unfl);
		*dstptr = result;
		/* inexact = FALSE */
		return(UNDERFLOWEXCEPTION);
		}
	    /*
	     * Since we cannot get an inexact denormalized result,
	     * we can now return.
	     */
	    Sgl_right_align(result,/*by*/(1-result_exponent),extent);
	    Sgl_clear_signexponent(result);
	    Sgl_set_sign(result,sign_save);
	    *dstptr = result;
	    return(NOEXCEPTION);
	    } /* end if(hidden...)... */
	/* Fall through and round */
	} /* end if(save >= 0)... */
    else 
	{
	/* Add magnitudes */
	Sgl_addition(left,right,/*to*/result);
	if(Sgl_isone_hiddenoverflow(result))
	    {
	    /* Prenormalization required. */
	    Sgl_rightshiftby1_withextent(result,extent,extent);
	    Sgl_arithrightshiftby1(result);
	    result_exponent++;
	    } /* end if hiddenoverflow... */
	} /* end else ...sub magnitudes... */
    
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
		  Sgl_isone_lowmantissa(result))
		    {
		    /* either exactly half way and odd or more than 1/2ulp */
		    Sgl_increment(result);
		    }
		}
	    break;

	    case ROUNDPLUS:
	    if(Sgl_iszero_sign(result))
		{
		/* Round up positive results */
		Sgl_increment(result);
		}
	    break;
	    
	    case ROUNDMINUS:
	    if(Sgl_isone_sign(result))
		{
		/* Round down negative results */
		Sgl_increment(result);
		}
	    
	    case ROUNDZERO:;
	    /* truncate is simple */
	    } /* end switch... */
	if(Sgl_isone_hiddenoverflow(result)) result_exponent++;
	}
    if(result_exponent == SGL_INFINITY_EXPONENT)
        {
        /* Overflow */
        if(Is_overflowtrap_enabled())
	    {
	    Sgl_setwrapped_exponent(result,result_exponent,ovfl);
	    *dstptr = result;
	    if (inexact)
		if (Is_inexacttrap_enabled())
		    return(OVERFLOWEXCEPTION | INEXACTEXCEPTION);
		else Set_inexactflag();
	    return(OVERFLOWEXCEPTION);
	    }
        else
	    {
	    Set_overflowflag();
	    inexact = TRUE;
	    Sgl_setoverflow(result);
	    }
	}
    else Sgl_set_exponent(result,result_exponent);
    *dstptr = result;
    if(inexact) 
	if(Is_inexacttrap_enabled()) return(INEXACTEXCEPTION);
	else Set_inexactflag();
    return(NOEXCEPTION);
    }
