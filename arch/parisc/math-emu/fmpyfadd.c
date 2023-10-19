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
 *	@(#)	pa/spmath/fmpyfadd.c		$Revision: 1.1 $
 *
 *  Purpose:
 *	Double Floating-point Multiply Fused Add
 *	Double Floating-point Multiply Negate Fused Add
 *	Single Floating-point Multiply Fused Add
 *	Single Floating-point Multiply Negate Fused Add
 *
 *  External Interfaces:
 *	dbl_fmpyfadd(src1ptr,src2ptr,src3ptr,status,dstptr)
 *	dbl_fmpynfadd(src1ptr,src2ptr,src3ptr,status,dstptr)
 *	sgl_fmpyfadd(src1ptr,src2ptr,src3ptr,status,dstptr)
 *	sgl_fmpynfadd(src1ptr,src2ptr,src3ptr,status,dstptr)
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


/*
 *  Double Floating-point Multiply Fused Add
 */

int
dbl_fmpyfadd(
	    dbl_floating_point *src1ptr,
	    dbl_floating_point *src2ptr,
	    dbl_floating_point *src3ptr,
	    unsigned int *status,
	    dbl_floating_point *dstptr)
{
	unsigned int opnd1p1, opnd1p2, opnd2p1, opnd2p2, opnd3p1, opnd3p2;
	register unsigned int tmpresp1, tmpresp2, tmpresp3, tmpresp4;
	unsigned int rightp1, rightp2, rightp3, rightp4;
	unsigned int resultp1, resultp2 = 0, resultp3 = 0, resultp4 = 0;
	register int mpy_exponent, add_exponent, count;
	boolean inexact = FALSE, is_tiny = FALSE;

	unsigned int signlessleft1, signlessright1, save;
	register int result_exponent, diff_exponent;
	int sign_save, jumpsize;
	
	Dbl_copyfromptr(src1ptr,opnd1p1,opnd1p2);
	Dbl_copyfromptr(src2ptr,opnd2p1,opnd2p2);
	Dbl_copyfromptr(src3ptr,opnd3p1,opnd3p2);

	/* 
	 * set sign bit of result of multiply
	 */
	if (Dbl_sign(opnd1p1) ^ Dbl_sign(opnd2p1)) 
		Dbl_setnegativezerop1(resultp1); 
	else Dbl_setzerop1(resultp1);

	/*
	 * Generate multiply exponent 
	 */
	mpy_exponent = Dbl_exponent(opnd1p1) + Dbl_exponent(opnd2p1) - DBL_BIAS;

	/*
	 * check first operand for NaN's or infinity
	 */
	if (Dbl_isinfinity_exponent(opnd1p1)) {
		if (Dbl_iszero_mantissa(opnd1p1,opnd1p2)) {
			if (Dbl_isnotnan(opnd2p1,opnd2p2) &&
			    Dbl_isnotnan(opnd3p1,opnd3p2)) {
				if (Dbl_iszero_exponentmantissa(opnd2p1,opnd2p2)) {
					/* 
					 * invalid since operands are infinity 
					 * and zero 
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Dbl_makequietnan(resultp1,resultp2);
					Dbl_copytoptr(resultp1,resultp2,dstptr);
					return(NOEXCEPTION);
				}
				/*
				 * Check third operand for infinity with a
				 *  sign opposite of the multiply result
				 */
				if (Dbl_isinfinity(opnd3p1,opnd3p2) &&
				    (Dbl_sign(resultp1) ^ Dbl_sign(opnd3p1))) {
					/* 
					 * invalid since attempting a magnitude
					 * subtraction of infinities
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
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
			    		return(OPC_2E_INVALIDEXCEPTION);
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
			    		return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Dbl_set_quiet(opnd2p1);
				Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
				return(NOEXCEPTION);
			}
			/* 
			 * is third operand a signaling NaN? 
			 */
			else if (Dbl_is_signalingnan(opnd3p1)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
			    		return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Dbl_set_quiet(opnd3p1);
				Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
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
			if (Dbl_isnotnan(opnd3p1,opnd3p2)) {
				if (Dbl_iszero_exponentmantissa(opnd1p1,opnd1p2)) {
					/* 
					 * invalid since multiply operands are
					 * zero & infinity
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Dbl_makequietnan(opnd2p1,opnd2p2);
					Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
					return(NOEXCEPTION);
				}

				/*
				 * Check third operand for infinity with a
				 *  sign opposite of the multiply result
				 */
				if (Dbl_isinfinity(opnd3p1,opnd3p2) &&
				    (Dbl_sign(resultp1) ^ Dbl_sign(opnd3p1))) {
					/* 
					 * invalid since attempting a magnitude
					 * subtraction of infinities
					 */
					if (Is_invalidtrap_enabled())
				       		return(OPC_2E_INVALIDEXCEPTION);
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
			if (Dbl_isone_signaling(opnd2p1)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
					return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Dbl_set_quiet(opnd2p1);
			}
			/* 
			 * is third operand a signaling NaN? 
			 */
			else if (Dbl_is_signalingnan(opnd3p1)) {
			       	/* trap if INVALIDTRAP enabled */
			       	if (Is_invalidtrap_enabled())
				   		return(OPC_2E_INVALIDEXCEPTION);
			       	/* make NaN quiet */
			       	Set_invalidflag();
			       	Dbl_set_quiet(opnd3p1);
				Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
		       		return(NOEXCEPTION);
			}
			/*
			 * return quiet NaN
			 */
			Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
			return(NOEXCEPTION);
		}
	}

	/*
	 * check third operand for NaN's or infinity
	 */
	if (Dbl_isinfinity_exponent(opnd3p1)) {
		if (Dbl_iszero_mantissa(opnd3p1,opnd3p2)) {
			/* return infinity */
			Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
			return(NOEXCEPTION);
		} else {
			/*
			 * is NaN; signaling or quiet?
			 */
			if (Dbl_isone_signaling(opnd3p1)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
					return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Dbl_set_quiet(opnd3p1);
			}
			/*
			 * return quiet NaN
 			 */
			Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
			return(NOEXCEPTION);
		}
    	}

	/*
	 * Generate multiply mantissa
	 */
	if (Dbl_isnotzero_exponent(opnd1p1)) {
		/* set hidden bit */
		Dbl_clear_signexponent_set_hidden(opnd1p1);
	}
	else {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd1p1,opnd1p2)) {
			/*
			 * Perform the add opnd3 with zero here.
			 */
			if (Dbl_iszero_exponentmantissa(opnd3p1,opnd3p2)) {
				if (Is_rounding_mode(ROUNDMINUS)) {
					Dbl_or_signs(opnd3p1,resultp1);
				} else {
					Dbl_and_signs(opnd3p1,resultp1);
				}
			}
			/*
			 * Now let's check for trapped underflow case.
			 */
			else if (Dbl_iszero_exponent(opnd3p1) &&
			         Is_underflowtrap_enabled()) {
                    		/* need to normalize results mantissa */
                    		sign_save = Dbl_signextendedsign(opnd3p1);
				result_exponent = 0;
                    		Dbl_leftshiftby1(opnd3p1,opnd3p2);
                    		Dbl_normalize(opnd3p1,opnd3p2,result_exponent);
                    		Dbl_set_sign(opnd3p1,/*using*/sign_save);
                    		Dbl_setwrapped_exponent(opnd3p1,result_exponent,
							unfl);
                    		Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
                    		/* inexact = FALSE */
                    		return(OPC_2E_UNDERFLOWEXCEPTION);
			}
			Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
			return(NOEXCEPTION);
		}
		/* is denormalized, adjust exponent */
		Dbl_clear_signexponent(opnd1p1);
		Dbl_leftshiftby1(opnd1p1,opnd1p2);
		Dbl_normalize(opnd1p1,opnd1p2,mpy_exponent);
	}
	/* opnd2 needs to have hidden bit set with msb in hidden bit */
	if (Dbl_isnotzero_exponent(opnd2p1)) {
		Dbl_clear_signexponent_set_hidden(opnd2p1);
	}
	else {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd2p1,opnd2p2)) {
			/*
			 * Perform the add opnd3 with zero here.
			 */
			if (Dbl_iszero_exponentmantissa(opnd3p1,opnd3p2)) {
				if (Is_rounding_mode(ROUNDMINUS)) {
					Dbl_or_signs(opnd3p1,resultp1);
				} else {
					Dbl_and_signs(opnd3p1,resultp1);
				}
			}
			/*
			 * Now let's check for trapped underflow case.
			 */
			else if (Dbl_iszero_exponent(opnd3p1) &&
			    Is_underflowtrap_enabled()) {
                    		/* need to normalize results mantissa */
                    		sign_save = Dbl_signextendedsign(opnd3p1);
				result_exponent = 0;
                    		Dbl_leftshiftby1(opnd3p1,opnd3p2);
                    		Dbl_normalize(opnd3p1,opnd3p2,result_exponent);
                    		Dbl_set_sign(opnd3p1,/*using*/sign_save);
                    		Dbl_setwrapped_exponent(opnd3p1,result_exponent,
							unfl);
                    		Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
                    		/* inexact = FALSE */
				return(OPC_2E_UNDERFLOWEXCEPTION);
			}
			Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
			return(NOEXCEPTION);
		}
		/* is denormalized; want to normalize */
		Dbl_clear_signexponent(opnd2p1);
		Dbl_leftshiftby1(opnd2p1,opnd2p2);
		Dbl_normalize(opnd2p1,opnd2p2,mpy_exponent);
	}

	/* Multiply the first two source mantissas together */

	/* 
	 * The intermediate result will be kept in tmpres,
	 * which needs enough room for 106 bits of mantissa,
	 * so lets call it a Double extended.
	 */
	Dblext_setzero(tmpresp1,tmpresp2,tmpresp3,tmpresp4);

	/* 
	 * Four bits at a time are inspected in each loop, and a 
	 * simple shift and add multiply algorithm is used. 
	 */ 
	for (count = DBL_P-1; count >= 0; count -= 4) {
		Dblext_rightshiftby4(tmpresp1,tmpresp2,tmpresp3,tmpresp4);
		if (Dbit28p2(opnd1p2)) {
	 		/* Fourword_add should be an ADD followed by 3 ADDC's */
			Fourword_add(tmpresp1, tmpresp2, tmpresp3, tmpresp4, 
			 opnd2p1<<3 | opnd2p2>>29, opnd2p2<<3, 0, 0);
		}
		if (Dbit29p2(opnd1p2)) {
			Fourword_add(tmpresp1, tmpresp2, tmpresp3, tmpresp4,
			 opnd2p1<<2 | opnd2p2>>30, opnd2p2<<2, 0, 0);
		}
		if (Dbit30p2(opnd1p2)) {
			Fourword_add(tmpresp1, tmpresp2, tmpresp3, tmpresp4,
			 opnd2p1<<1 | opnd2p2>>31, opnd2p2<<1, 0, 0);
		}
		if (Dbit31p2(opnd1p2)) {
			Fourword_add(tmpresp1, tmpresp2, tmpresp3, tmpresp4,
			 opnd2p1, opnd2p2, 0, 0);
		}
		Dbl_rightshiftby4(opnd1p1,opnd1p2);
	}
	if (Is_dexthiddenoverflow(tmpresp1)) {
		/* result mantissa >= 2 (mantissa overflow) */
		mpy_exponent++;
		Dblext_rightshiftby1(tmpresp1,tmpresp2,tmpresp3,tmpresp4);
	}

	/*
	 * Restore the sign of the mpy result which was saved in resultp1.
	 * The exponent will continue to be kept in mpy_exponent.
	 */
	Dblext_set_sign(tmpresp1,Dbl_sign(resultp1));

	/* 
	 * No rounding is required, since the result of the multiply
	 * is exact in the extended format.
	 */

	/*
	 * Now we are ready to perform the add portion of the operation.
	 *
	 * The exponents need to be kept as integers for now, since the
	 * multiply result might not fit into the exponent field.  We
	 * can't overflow or underflow because of this yet, since the
	 * add could bring the final result back into range.
	 */
	add_exponent = Dbl_exponent(opnd3p1);

	/*
	 * Check for denormalized or zero add operand.
	 */
	if (add_exponent == 0) {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd3p1,opnd3p2)) {
			/* right is zero */
			/* Left can't be zero and must be result.
			 *
			 * The final result is now in tmpres and mpy_exponent,
			 * and needs to be rounded and squeezed back into
			 * double precision format from double extended.
			 */
			result_exponent = mpy_exponent;
			Dblext_copy(tmpresp1,tmpresp2,tmpresp3,tmpresp4,
				resultp1,resultp2,resultp3,resultp4);
			sign_save = Dbl_signextendedsign(resultp1);/*save sign*/
			goto round;
		}

		/* 
		 * Neither are zeroes.  
		 * Adjust exponent and normalize add operand.
		 */
		sign_save = Dbl_signextendedsign(opnd3p1);	/* save sign */
		Dbl_clear_signexponent(opnd3p1);
		Dbl_leftshiftby1(opnd3p1,opnd3p2);
		Dbl_normalize(opnd3p1,opnd3p2,add_exponent);
		Dbl_set_sign(opnd3p1,sign_save);	/* restore sign */
	} else {
		Dbl_clear_exponent_set_hidden(opnd3p1);
	}
	/*
	 * Copy opnd3 to the double extended variable called right.
	 */
	Dbl_copyto_dblext(opnd3p1,opnd3p2,rightp1,rightp2,rightp3,rightp4);

	/*
	 * A zero "save" helps discover equal operands (for later),
	 * and is used in swapping operands (if needed).
	 */
	Dblext_xortointp1(tmpresp1,rightp1,/*to*/save);

	/*
	 * Compare magnitude of operands.
	 */
	Dblext_copytoint_exponentmantissap1(tmpresp1,signlessleft1);
	Dblext_copytoint_exponentmantissap1(rightp1,signlessright1);
	if (mpy_exponent < add_exponent || mpy_exponent == add_exponent &&
	    Dblext_ismagnitudeless(tmpresp2,rightp2,signlessleft1,signlessright1)){
		/*
		 * Set the left operand to the larger one by XOR swap.
		 * First finish the first word "save".
		 */
		Dblext_xorfromintp1(save,rightp1,/*to*/rightp1);
		Dblext_xorfromintp1(save,tmpresp1,/*to*/tmpresp1);
		Dblext_swap_lower(tmpresp2,tmpresp3,tmpresp4,
			rightp2,rightp3,rightp4);
		/* also setup exponents used in rest of routine */
		diff_exponent = add_exponent - mpy_exponent;
		result_exponent = add_exponent;
	} else {
		/* also setup exponents used in rest of routine */
		diff_exponent = mpy_exponent - add_exponent;
		result_exponent = mpy_exponent;
	}
	/* Invariant: left is not smaller than right. */

	/*
	 * Special case alignment of operands that would force alignment
	 * beyond the extent of the extension.  A further optimization
	 * could special case this but only reduces the path length for
	 * this infrequent case.
	 */
	if (diff_exponent > DBLEXT_THRESHOLD) {
		diff_exponent = DBLEXT_THRESHOLD;
	}

	/* Align right operand by shifting it to the right */
	Dblext_clear_sign(rightp1);
	Dblext_right_align(rightp1,rightp2,rightp3,rightp4,
		/*shifted by*/diff_exponent);
	
	/* Treat sum and difference of the operands separately. */
	if ((int)save < 0) {
		/*
		 * Difference of the two operands.  Overflow can occur if the
		 * multiply overflowed.  A borrow can occur out of the hidden
		 * bit and force a post normalization phase.
		 */
		Dblext_subtract(tmpresp1,tmpresp2,tmpresp3,tmpresp4,
			rightp1,rightp2,rightp3,rightp4,
			resultp1,resultp2,resultp3,resultp4);
		sign_save = Dbl_signextendedsign(resultp1);
		if (Dbl_iszero_hidden(resultp1)) {
			/* Handle normalization */
		/* A straightforward algorithm would now shift the
		 * result and extension left until the hidden bit
		 * becomes one.  Not all of the extension bits need
		 * participate in the shift.  Only the two most 
		 * significant bits (round and guard) are needed.
		 * If only a single shift is needed then the guard
		 * bit becomes a significant low order bit and the
		 * extension must participate in the rounding.
		 * If more than a single shift is needed, then all
		 * bits to the right of the guard bit are zeros, 
		 * and the guard bit may or may not be zero. */
			Dblext_leftshiftby1(resultp1,resultp2,resultp3,
				resultp4);

			/* Need to check for a zero result.  The sign and
			 * exponent fields have already been zeroed.  The more
			 * efficient test of the full object can be used.
			 */
			 if(Dblext_iszero(resultp1,resultp2,resultp3,resultp4)){
				/* Must have been "x-x" or "x+(-x)". */
				if (Is_rounding_mode(ROUNDMINUS))
					Dbl_setone_sign(resultp1);
				Dbl_copytoptr(resultp1,resultp2,dstptr);
				return(NOEXCEPTION);
			}
			result_exponent--;

			/* Look to see if normalization is finished. */
			if (Dbl_isone_hidden(resultp1)) {
				/* No further normalization is needed */
				goto round;
			}

			/* Discover first one bit to determine shift amount.
			 * Use a modified binary search.  We have already
			 * shifted the result one position right and still
			 * not found a one so the remainder of the extension
			 * must be zero and simplifies rounding. */
			/* Scan bytes */
			while (Dbl_iszero_hiddenhigh7mantissa(resultp1)) {
				Dblext_leftshiftby8(resultp1,resultp2,resultp3,resultp4);
				result_exponent -= 8;
			}
			/* Now narrow it down to the nibble */
			if (Dbl_iszero_hiddenhigh3mantissa(resultp1)) {
				/* The lower nibble contains the
				 * normalizing one */
				Dblext_leftshiftby4(resultp1,resultp2,resultp3,resultp4);
				result_exponent -= 4;
			}
			/* Select case where first bit is set (already
			 * normalized) otherwise select the proper shift. */
			jumpsize = Dbl_hiddenhigh3mantissa(resultp1);
			if (jumpsize <= 7) switch(jumpsize) {
			case 1:
				Dblext_leftshiftby3(resultp1,resultp2,resultp3,
					resultp4);
				result_exponent -= 3;
				break;
			case 2:
			case 3:
				Dblext_leftshiftby2(resultp1,resultp2,resultp3,
					resultp4);
				result_exponent -= 2;
				break;
			case 4:
			case 5:
			case 6:
			case 7:
				Dblext_leftshiftby1(resultp1,resultp2,resultp3,
					resultp4);
				result_exponent -= 1;
				break;
			}
		} /* end if (hidden...)... */
	/* Fall through and round */
	} /* end if (save < 0)... */
	else {
		/* Add magnitudes */
		Dblext_addition(tmpresp1,tmpresp2,tmpresp3,tmpresp4,
			rightp1,rightp2,rightp3,rightp4,
			/*to*/resultp1,resultp2,resultp3,resultp4);
		sign_save = Dbl_signextendedsign(resultp1);
		if (Dbl_isone_hiddenoverflow(resultp1)) {
	    		/* Prenormalization required. */
	    		Dblext_arithrightshiftby1(resultp1,resultp2,resultp3,
				resultp4);
	    		result_exponent++;
		} /* end if hiddenoverflow... */
	} /* end else ...add magnitudes... */

	/* Round the result.  If the extension and lower two words are
	 * all zeros, then the result is exact.  Otherwise round in the
	 * correct direction.  Underflow is possible. If a postnormalization
	 * is necessary, then the mantissa is all zeros so no shift is needed.
	 */
  round:
	if (result_exponent <= 0 && !Is_underflowtrap_enabled()) {
		Dblext_denormalize(resultp1,resultp2,resultp3,resultp4,
			result_exponent,is_tiny);
	}
	Dbl_set_sign(resultp1,/*using*/sign_save);
	if (Dblext_isnotzero_mantissap3(resultp3) || 
	    Dblext_isnotzero_mantissap4(resultp4)) {
		inexact = TRUE;
		switch(Rounding_mode()) {
		case ROUNDNEAREST: /* The default. */
			if (Dblext_isone_highp3(resultp3)) {
				/* at least 1/2 ulp */
				if (Dblext_isnotzero_low31p3(resultp3) ||
				    Dblext_isnotzero_mantissap4(resultp4) ||
				    Dblext_isone_lowp2(resultp2)) {
					/* either exactly half way and odd or
					 * more than 1/2ulp */
					Dbl_increment(resultp1,resultp2);
				}
			}
	    		break;

		case ROUNDPLUS:
	    		if (Dbl_iszero_sign(resultp1)) {
				/* Round up positive results */
				Dbl_increment(resultp1,resultp2);
			}
			break;
	    
		case ROUNDMINUS:
	    		if (Dbl_isone_sign(resultp1)) {
				/* Round down negative results */
				Dbl_increment(resultp1,resultp2);
			}
	    
		case ROUNDZERO:;
			/* truncate is simple */
		} /* end switch... */
		if (Dbl_isone_hiddenoverflow(resultp1)) result_exponent++;
	}
	if (result_exponent >= DBL_INFINITY_EXPONENT) {
                /* trap if OVERFLOWTRAP enabled */
                if (Is_overflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                        Dbl_setwrapped_exponent(resultp1,result_exponent,ovfl);
                        Dbl_copytoptr(resultp1,resultp2,dstptr);
                        if (inexact)
                            if (Is_inexacttrap_enabled())
                                return (OPC_2E_OVERFLOWEXCEPTION |
					OPC_2E_INEXACTEXCEPTION);
                            else Set_inexactflag();
                        return (OPC_2E_OVERFLOWEXCEPTION);
                }
                inexact = TRUE;
                Set_overflowflag();
                /* set result to infinity or largest number */
                Dbl_setoverflow(resultp1,resultp2);

	} else if (result_exponent <= 0) {	/* underflow case */
		if (Is_underflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                	Dbl_setwrapped_exponent(resultp1,result_exponent,unfl);
			Dbl_copytoptr(resultp1,resultp2,dstptr);
                        if (inexact)
                            if (Is_inexacttrap_enabled())
                                return (OPC_2E_UNDERFLOWEXCEPTION |
					OPC_2E_INEXACTEXCEPTION);
                            else Set_inexactflag();
	    		return(OPC_2E_UNDERFLOWEXCEPTION);
		}
		else if (inexact && is_tiny) Set_underflowflag();
	}
	else Dbl_set_exponent(resultp1,result_exponent);
	Dbl_copytoptr(resultp1,resultp2,dstptr);
	if (inexact) 
		if (Is_inexacttrap_enabled()) return(OPC_2E_INEXACTEXCEPTION);
		else Set_inexactflag();
    	return(NOEXCEPTION);
}

/*
 *  Double Floating-point Multiply Negate Fused Add
 */

dbl_fmpynfadd(src1ptr,src2ptr,src3ptr,status,dstptr)

dbl_floating_point *src1ptr, *src2ptr, *src3ptr, *dstptr;
unsigned int *status;
{
	unsigned int opnd1p1, opnd1p2, opnd2p1, opnd2p2, opnd3p1, opnd3p2;
	register unsigned int tmpresp1, tmpresp2, tmpresp3, tmpresp4;
	unsigned int rightp1, rightp2, rightp3, rightp4;
	unsigned int resultp1, resultp2 = 0, resultp3 = 0, resultp4 = 0;
	register int mpy_exponent, add_exponent, count;
	boolean inexact = FALSE, is_tiny = FALSE;

	unsigned int signlessleft1, signlessright1, save;
	register int result_exponent, diff_exponent;
	int sign_save, jumpsize;
	
	Dbl_copyfromptr(src1ptr,opnd1p1,opnd1p2);
	Dbl_copyfromptr(src2ptr,opnd2p1,opnd2p2);
	Dbl_copyfromptr(src3ptr,opnd3p1,opnd3p2);

	/* 
	 * set sign bit of result of multiply
	 */
	if (Dbl_sign(opnd1p1) ^ Dbl_sign(opnd2p1)) 
		Dbl_setzerop1(resultp1);
	else
		Dbl_setnegativezerop1(resultp1); 

	/*
	 * Generate multiply exponent 
	 */
	mpy_exponent = Dbl_exponent(opnd1p1) + Dbl_exponent(opnd2p1) - DBL_BIAS;

	/*
	 * check first operand for NaN's or infinity
	 */
	if (Dbl_isinfinity_exponent(opnd1p1)) {
		if (Dbl_iszero_mantissa(opnd1p1,opnd1p2)) {
			if (Dbl_isnotnan(opnd2p1,opnd2p2) &&
			    Dbl_isnotnan(opnd3p1,opnd3p2)) {
				if (Dbl_iszero_exponentmantissa(opnd2p1,opnd2p2)) {
					/* 
					 * invalid since operands are infinity 
					 * and zero 
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Dbl_makequietnan(resultp1,resultp2);
					Dbl_copytoptr(resultp1,resultp2,dstptr);
					return(NOEXCEPTION);
				}
				/*
				 * Check third operand for infinity with a
				 *  sign opposite of the multiply result
				 */
				if (Dbl_isinfinity(opnd3p1,opnd3p2) &&
				    (Dbl_sign(resultp1) ^ Dbl_sign(opnd3p1))) {
					/* 
					 * invalid since attempting a magnitude
					 * subtraction of infinities
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
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
			    		return(OPC_2E_INVALIDEXCEPTION);
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
			    		return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Dbl_set_quiet(opnd2p1);
				Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
				return(NOEXCEPTION);
			}
			/* 
			 * is third operand a signaling NaN? 
			 */
			else if (Dbl_is_signalingnan(opnd3p1)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
			    		return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Dbl_set_quiet(opnd3p1);
				Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
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
			if (Dbl_isnotnan(opnd3p1,opnd3p2)) {
				if (Dbl_iszero_exponentmantissa(opnd1p1,opnd1p2)) {
					/* 
					 * invalid since multiply operands are
					 * zero & infinity
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Dbl_makequietnan(opnd2p1,opnd2p2);
					Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
					return(NOEXCEPTION);
				}

				/*
				 * Check third operand for infinity with a
				 *  sign opposite of the multiply result
				 */
				if (Dbl_isinfinity(opnd3p1,opnd3p2) &&
				    (Dbl_sign(resultp1) ^ Dbl_sign(opnd3p1))) {
					/* 
					 * invalid since attempting a magnitude
					 * subtraction of infinities
					 */
					if (Is_invalidtrap_enabled())
				       		return(OPC_2E_INVALIDEXCEPTION);
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
			if (Dbl_isone_signaling(opnd2p1)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
					return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Dbl_set_quiet(opnd2p1);
			}
			/* 
			 * is third operand a signaling NaN? 
			 */
			else if (Dbl_is_signalingnan(opnd3p1)) {
			       	/* trap if INVALIDTRAP enabled */
			       	if (Is_invalidtrap_enabled())
				   		return(OPC_2E_INVALIDEXCEPTION);
			       	/* make NaN quiet */
			       	Set_invalidflag();
			       	Dbl_set_quiet(opnd3p1);
				Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
		       		return(NOEXCEPTION);
			}
			/*
			 * return quiet NaN
			 */
			Dbl_copytoptr(opnd2p1,opnd2p2,dstptr);
			return(NOEXCEPTION);
		}
	}

	/*
	 * check third operand for NaN's or infinity
	 */
	if (Dbl_isinfinity_exponent(opnd3p1)) {
		if (Dbl_iszero_mantissa(opnd3p1,opnd3p2)) {
			/* return infinity */
			Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
			return(NOEXCEPTION);
		} else {
			/*
			 * is NaN; signaling or quiet?
			 */
			if (Dbl_isone_signaling(opnd3p1)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
					return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Dbl_set_quiet(opnd3p1);
			}
			/*
			 * return quiet NaN
 			 */
			Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
			return(NOEXCEPTION);
		}
    	}

	/*
	 * Generate multiply mantissa
	 */
	if (Dbl_isnotzero_exponent(opnd1p1)) {
		/* set hidden bit */
		Dbl_clear_signexponent_set_hidden(opnd1p1);
	}
	else {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd1p1,opnd1p2)) {
			/*
			 * Perform the add opnd3 with zero here.
			 */
			if (Dbl_iszero_exponentmantissa(opnd3p1,opnd3p2)) {
				if (Is_rounding_mode(ROUNDMINUS)) {
					Dbl_or_signs(opnd3p1,resultp1);
				} else {
					Dbl_and_signs(opnd3p1,resultp1);
				}
			}
			/*
			 * Now let's check for trapped underflow case.
			 */
			else if (Dbl_iszero_exponent(opnd3p1) &&
			         Is_underflowtrap_enabled()) {
                    		/* need to normalize results mantissa */
                    		sign_save = Dbl_signextendedsign(opnd3p1);
				result_exponent = 0;
                    		Dbl_leftshiftby1(opnd3p1,opnd3p2);
                    		Dbl_normalize(opnd3p1,opnd3p2,result_exponent);
                    		Dbl_set_sign(opnd3p1,/*using*/sign_save);
                    		Dbl_setwrapped_exponent(opnd3p1,result_exponent,
							unfl);
                    		Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
                    		/* inexact = FALSE */
                    		return(OPC_2E_UNDERFLOWEXCEPTION);
			}
			Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
			return(NOEXCEPTION);
		}
		/* is denormalized, adjust exponent */
		Dbl_clear_signexponent(opnd1p1);
		Dbl_leftshiftby1(opnd1p1,opnd1p2);
		Dbl_normalize(opnd1p1,opnd1p2,mpy_exponent);
	}
	/* opnd2 needs to have hidden bit set with msb in hidden bit */
	if (Dbl_isnotzero_exponent(opnd2p1)) {
		Dbl_clear_signexponent_set_hidden(opnd2p1);
	}
	else {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd2p1,opnd2p2)) {
			/*
			 * Perform the add opnd3 with zero here.
			 */
			if (Dbl_iszero_exponentmantissa(opnd3p1,opnd3p2)) {
				if (Is_rounding_mode(ROUNDMINUS)) {
					Dbl_or_signs(opnd3p1,resultp1);
				} else {
					Dbl_and_signs(opnd3p1,resultp1);
				}
			}
			/*
			 * Now let's check for trapped underflow case.
			 */
			else if (Dbl_iszero_exponent(opnd3p1) &&
			    Is_underflowtrap_enabled()) {
                    		/* need to normalize results mantissa */
                    		sign_save = Dbl_signextendedsign(opnd3p1);
				result_exponent = 0;
                    		Dbl_leftshiftby1(opnd3p1,opnd3p2);
                    		Dbl_normalize(opnd3p1,opnd3p2,result_exponent);
                    		Dbl_set_sign(opnd3p1,/*using*/sign_save);
                    		Dbl_setwrapped_exponent(opnd3p1,result_exponent,
							unfl);
                    		Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
                    		/* inexact = FALSE */
                    		return(OPC_2E_UNDERFLOWEXCEPTION);
			}
			Dbl_copytoptr(opnd3p1,opnd3p2,dstptr);
			return(NOEXCEPTION);
		}
		/* is denormalized; want to normalize */
		Dbl_clear_signexponent(opnd2p1);
		Dbl_leftshiftby1(opnd2p1,opnd2p2);
		Dbl_normalize(opnd2p1,opnd2p2,mpy_exponent);
	}

	/* Multiply the first two source mantissas together */

	/* 
	 * The intermediate result will be kept in tmpres,
	 * which needs enough room for 106 bits of mantissa,
	 * so lets call it a Double extended.
	 */
	Dblext_setzero(tmpresp1,tmpresp2,tmpresp3,tmpresp4);

	/* 
	 * Four bits at a time are inspected in each loop, and a 
	 * simple shift and add multiply algorithm is used. 
	 */ 
	for (count = DBL_P-1; count >= 0; count -= 4) {
		Dblext_rightshiftby4(tmpresp1,tmpresp2,tmpresp3,tmpresp4);
		if (Dbit28p2(opnd1p2)) {
	 		/* Fourword_add should be an ADD followed by 3 ADDC's */
			Fourword_add(tmpresp1, tmpresp2, tmpresp3, tmpresp4, 
			 opnd2p1<<3 | opnd2p2>>29, opnd2p2<<3, 0, 0);
		}
		if (Dbit29p2(opnd1p2)) {
			Fourword_add(tmpresp1, tmpresp2, tmpresp3, tmpresp4,
			 opnd2p1<<2 | opnd2p2>>30, opnd2p2<<2, 0, 0);
		}
		if (Dbit30p2(opnd1p2)) {
			Fourword_add(tmpresp1, tmpresp2, tmpresp3, tmpresp4,
			 opnd2p1<<1 | opnd2p2>>31, opnd2p2<<1, 0, 0);
		}
		if (Dbit31p2(opnd1p2)) {
			Fourword_add(tmpresp1, tmpresp2, tmpresp3, tmpresp4,
			 opnd2p1, opnd2p2, 0, 0);
		}
		Dbl_rightshiftby4(opnd1p1,opnd1p2);
	}
	if (Is_dexthiddenoverflow(tmpresp1)) {
		/* result mantissa >= 2 (mantissa overflow) */
		mpy_exponent++;
		Dblext_rightshiftby1(tmpresp1,tmpresp2,tmpresp3,tmpresp4);
	}

	/*
	 * Restore the sign of the mpy result which was saved in resultp1.
	 * The exponent will continue to be kept in mpy_exponent.
	 */
	Dblext_set_sign(tmpresp1,Dbl_sign(resultp1));

	/* 
	 * No rounding is required, since the result of the multiply
	 * is exact in the extended format.
	 */

	/*
	 * Now we are ready to perform the add portion of the operation.
	 *
	 * The exponents need to be kept as integers for now, since the
	 * multiply result might not fit into the exponent field.  We
	 * can't overflow or underflow because of this yet, since the
	 * add could bring the final result back into range.
	 */
	add_exponent = Dbl_exponent(opnd3p1);

	/*
	 * Check for denormalized or zero add operand.
	 */
	if (add_exponent == 0) {
		/* check for zero */
		if (Dbl_iszero_mantissa(opnd3p1,opnd3p2)) {
			/* right is zero */
			/* Left can't be zero and must be result.
			 *
			 * The final result is now in tmpres and mpy_exponent,
			 * and needs to be rounded and squeezed back into
			 * double precision format from double extended.
			 */
			result_exponent = mpy_exponent;
			Dblext_copy(tmpresp1,tmpresp2,tmpresp3,tmpresp4,
				resultp1,resultp2,resultp3,resultp4);
			sign_save = Dbl_signextendedsign(resultp1);/*save sign*/
			goto round;
		}

		/* 
		 * Neither are zeroes.  
		 * Adjust exponent and normalize add operand.
		 */
		sign_save = Dbl_signextendedsign(opnd3p1);	/* save sign */
		Dbl_clear_signexponent(opnd3p1);
		Dbl_leftshiftby1(opnd3p1,opnd3p2);
		Dbl_normalize(opnd3p1,opnd3p2,add_exponent);
		Dbl_set_sign(opnd3p1,sign_save);	/* restore sign */
	} else {
		Dbl_clear_exponent_set_hidden(opnd3p1);
	}
	/*
	 * Copy opnd3 to the double extended variable called right.
	 */
	Dbl_copyto_dblext(opnd3p1,opnd3p2,rightp1,rightp2,rightp3,rightp4);

	/*
	 * A zero "save" helps discover equal operands (for later),
	 * and is used in swapping operands (if needed).
	 */
	Dblext_xortointp1(tmpresp1,rightp1,/*to*/save);

	/*
	 * Compare magnitude of operands.
	 */
	Dblext_copytoint_exponentmantissap1(tmpresp1,signlessleft1);
	Dblext_copytoint_exponentmantissap1(rightp1,signlessright1);
	if (mpy_exponent < add_exponent || mpy_exponent == add_exponent &&
	    Dblext_ismagnitudeless(tmpresp2,rightp2,signlessleft1,signlessright1)){
		/*
		 * Set the left operand to the larger one by XOR swap.
		 * First finish the first word "save".
		 */
		Dblext_xorfromintp1(save,rightp1,/*to*/rightp1);
		Dblext_xorfromintp1(save,tmpresp1,/*to*/tmpresp1);
		Dblext_swap_lower(tmpresp2,tmpresp3,tmpresp4,
			rightp2,rightp3,rightp4);
		/* also setup exponents used in rest of routine */
		diff_exponent = add_exponent - mpy_exponent;
		result_exponent = add_exponent;
	} else {
		/* also setup exponents used in rest of routine */
		diff_exponent = mpy_exponent - add_exponent;
		result_exponent = mpy_exponent;
	}
	/* Invariant: left is not smaller than right. */

	/*
	 * Special case alignment of operands that would force alignment
	 * beyond the extent of the extension.  A further optimization
	 * could special case this but only reduces the path length for
	 * this infrequent case.
	 */
	if (diff_exponent > DBLEXT_THRESHOLD) {
		diff_exponent = DBLEXT_THRESHOLD;
	}

	/* Align right operand by shifting it to the right */
	Dblext_clear_sign(rightp1);
	Dblext_right_align(rightp1,rightp2,rightp3,rightp4,
		/*shifted by*/diff_exponent);
	
	/* Treat sum and difference of the operands separately. */
	if ((int)save < 0) {
		/*
		 * Difference of the two operands.  Overflow can occur if the
		 * multiply overflowed.  A borrow can occur out of the hidden
		 * bit and force a post normalization phase.
		 */
		Dblext_subtract(tmpresp1,tmpresp2,tmpresp3,tmpresp4,
			rightp1,rightp2,rightp3,rightp4,
			resultp1,resultp2,resultp3,resultp4);
		sign_save = Dbl_signextendedsign(resultp1);
		if (Dbl_iszero_hidden(resultp1)) {
			/* Handle normalization */
		/* A straightforward algorithm would now shift the
		 * result and extension left until the hidden bit
		 * becomes one.  Not all of the extension bits need
		 * participate in the shift.  Only the two most 
		 * significant bits (round and guard) are needed.
		 * If only a single shift is needed then the guard
		 * bit becomes a significant low order bit and the
		 * extension must participate in the rounding.
		 * If more than a single shift is needed, then all
		 * bits to the right of the guard bit are zeros, 
		 * and the guard bit may or may not be zero. */
			Dblext_leftshiftby1(resultp1,resultp2,resultp3,
				resultp4);

			/* Need to check for a zero result.  The sign and
			 * exponent fields have already been zeroed.  The more
			 * efficient test of the full object can be used.
			 */
			 if (Dblext_iszero(resultp1,resultp2,resultp3,resultp4)) {
				/* Must have been "x-x" or "x+(-x)". */
				if (Is_rounding_mode(ROUNDMINUS))
					Dbl_setone_sign(resultp1);
				Dbl_copytoptr(resultp1,resultp2,dstptr);
				return(NOEXCEPTION);
			}
			result_exponent--;

			/* Look to see if normalization is finished. */
			if (Dbl_isone_hidden(resultp1)) {
				/* No further normalization is needed */
				goto round;
			}

			/* Discover first one bit to determine shift amount.
			 * Use a modified binary search.  We have already
			 * shifted the result one position right and still
			 * not found a one so the remainder of the extension
			 * must be zero and simplifies rounding. */
			/* Scan bytes */
			while (Dbl_iszero_hiddenhigh7mantissa(resultp1)) {
				Dblext_leftshiftby8(resultp1,resultp2,resultp3,resultp4);
				result_exponent -= 8;
			}
			/* Now narrow it down to the nibble */
			if (Dbl_iszero_hiddenhigh3mantissa(resultp1)) {
				/* The lower nibble contains the
				 * normalizing one */
				Dblext_leftshiftby4(resultp1,resultp2,resultp3,resultp4);
				result_exponent -= 4;
			}
			/* Select case where first bit is set (already
			 * normalized) otherwise select the proper shift. */
			jumpsize = Dbl_hiddenhigh3mantissa(resultp1);
			if (jumpsize <= 7) switch(jumpsize) {
			case 1:
				Dblext_leftshiftby3(resultp1,resultp2,resultp3,
					resultp4);
				result_exponent -= 3;
				break;
			case 2:
			case 3:
				Dblext_leftshiftby2(resultp1,resultp2,resultp3,
					resultp4);
				result_exponent -= 2;
				break;
			case 4:
			case 5:
			case 6:
			case 7:
				Dblext_leftshiftby1(resultp1,resultp2,resultp3,
					resultp4);
				result_exponent -= 1;
				break;
			}
		} /* end if (hidden...)... */
	/* Fall through and round */
	} /* end if (save < 0)... */
	else {
		/* Add magnitudes */
		Dblext_addition(tmpresp1,tmpresp2,tmpresp3,tmpresp4,
			rightp1,rightp2,rightp3,rightp4,
			/*to*/resultp1,resultp2,resultp3,resultp4);
		sign_save = Dbl_signextendedsign(resultp1);
		if (Dbl_isone_hiddenoverflow(resultp1)) {
	    		/* Prenormalization required. */
	    		Dblext_arithrightshiftby1(resultp1,resultp2,resultp3,
				resultp4);
	    		result_exponent++;
		} /* end if hiddenoverflow... */
	} /* end else ...add magnitudes... */

	/* Round the result.  If the extension and lower two words are
	 * all zeros, then the result is exact.  Otherwise round in the
	 * correct direction.  Underflow is possible. If a postnormalization
	 * is necessary, then the mantissa is all zeros so no shift is needed.
	 */
  round:
	if (result_exponent <= 0 && !Is_underflowtrap_enabled()) {
		Dblext_denormalize(resultp1,resultp2,resultp3,resultp4,
			result_exponent,is_tiny);
	}
	Dbl_set_sign(resultp1,/*using*/sign_save);
	if (Dblext_isnotzero_mantissap3(resultp3) || 
	    Dblext_isnotzero_mantissap4(resultp4)) {
		inexact = TRUE;
		switch(Rounding_mode()) {
		case ROUNDNEAREST: /* The default. */
			if (Dblext_isone_highp3(resultp3)) {
				/* at least 1/2 ulp */
				if (Dblext_isnotzero_low31p3(resultp3) ||
				    Dblext_isnotzero_mantissap4(resultp4) ||
				    Dblext_isone_lowp2(resultp2)) {
					/* either exactly half way and odd or
					 * more than 1/2ulp */
					Dbl_increment(resultp1,resultp2);
				}
			}
	    		break;

		case ROUNDPLUS:
	    		if (Dbl_iszero_sign(resultp1)) {
				/* Round up positive results */
				Dbl_increment(resultp1,resultp2);
			}
			break;
	    
		case ROUNDMINUS:
	    		if (Dbl_isone_sign(resultp1)) {
				/* Round down negative results */
				Dbl_increment(resultp1,resultp2);
			}
	    
		case ROUNDZERO:;
			/* truncate is simple */
		} /* end switch... */
		if (Dbl_isone_hiddenoverflow(resultp1)) result_exponent++;
	}
	if (result_exponent >= DBL_INFINITY_EXPONENT) {
		/* Overflow */
		if (Is_overflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                        Dbl_setwrapped_exponent(resultp1,result_exponent,ovfl);
                        Dbl_copytoptr(resultp1,resultp2,dstptr);
                        if (inexact)
                            if (Is_inexacttrap_enabled())
                                return (OPC_2E_OVERFLOWEXCEPTION |
					OPC_2E_INEXACTEXCEPTION);
                            else Set_inexactflag();
                        return (OPC_2E_OVERFLOWEXCEPTION);
		}
		inexact = TRUE;
		Set_overflowflag();
		Dbl_setoverflow(resultp1,resultp2);
	} else if (result_exponent <= 0) {	/* underflow case */
		if (Is_underflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                	Dbl_setwrapped_exponent(resultp1,result_exponent,unfl);
			Dbl_copytoptr(resultp1,resultp2,dstptr);
                        if (inexact)
                            if (Is_inexacttrap_enabled())
                                return (OPC_2E_UNDERFLOWEXCEPTION |
					OPC_2E_INEXACTEXCEPTION);
                            else Set_inexactflag();
	    		return(OPC_2E_UNDERFLOWEXCEPTION);
		}
		else if (inexact && is_tiny) Set_underflowflag();
	}
	else Dbl_set_exponent(resultp1,result_exponent);
	Dbl_copytoptr(resultp1,resultp2,dstptr);
	if (inexact) 
		if (Is_inexacttrap_enabled()) return(OPC_2E_INEXACTEXCEPTION);
		else Set_inexactflag();
    	return(NOEXCEPTION);
}

/*
 *  Single Floating-point Multiply Fused Add
 */

sgl_fmpyfadd(src1ptr,src2ptr,src3ptr,status,dstptr)

sgl_floating_point *src1ptr, *src2ptr, *src3ptr, *dstptr;
unsigned int *status;
{
	unsigned int opnd1, opnd2, opnd3;
	register unsigned int tmpresp1, tmpresp2;
	unsigned int rightp1, rightp2;
	unsigned int resultp1, resultp2 = 0;
	register int mpy_exponent, add_exponent, count;
	boolean inexact = FALSE, is_tiny = FALSE;

	unsigned int signlessleft1, signlessright1, save;
	register int result_exponent, diff_exponent;
	int sign_save, jumpsize;
	
	Sgl_copyfromptr(src1ptr,opnd1);
	Sgl_copyfromptr(src2ptr,opnd2);
	Sgl_copyfromptr(src3ptr,opnd3);

	/* 
	 * set sign bit of result of multiply
	 */
	if (Sgl_sign(opnd1) ^ Sgl_sign(opnd2)) 
		Sgl_setnegativezero(resultp1); 
	else Sgl_setzero(resultp1);

	/*
	 * Generate multiply exponent 
	 */
	mpy_exponent = Sgl_exponent(opnd1) + Sgl_exponent(opnd2) - SGL_BIAS;

	/*
	 * check first operand for NaN's or infinity
	 */
	if (Sgl_isinfinity_exponent(opnd1)) {
		if (Sgl_iszero_mantissa(opnd1)) {
			if (Sgl_isnotnan(opnd2) && Sgl_isnotnan(opnd3)) {
				if (Sgl_iszero_exponentmantissa(opnd2)) {
					/* 
					 * invalid since operands are infinity 
					 * and zero 
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Sgl_makequietnan(resultp1);
					Sgl_copytoptr(resultp1,dstptr);
					return(NOEXCEPTION);
				}
				/*
				 * Check third operand for infinity with a
				 *  sign opposite of the multiply result
				 */
				if (Sgl_isinfinity(opnd3) &&
				    (Sgl_sign(resultp1) ^ Sgl_sign(opnd3))) {
					/* 
					 * invalid since attempting a magnitude
					 * subtraction of infinities
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Sgl_makequietnan(resultp1);
					Sgl_copytoptr(resultp1,dstptr);
					return(NOEXCEPTION);
				}

				/*
			 	 * return infinity
			 	 */
				Sgl_setinfinity_exponentmantissa(resultp1);
				Sgl_copytoptr(resultp1,dstptr);
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
			    		return(OPC_2E_INVALIDEXCEPTION);
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
			    		return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Sgl_set_quiet(opnd2);
				Sgl_copytoptr(opnd2,dstptr);
				return(NOEXCEPTION);
			}
			/* 
			 * is third operand a signaling NaN? 
			 */
			else if (Sgl_is_signalingnan(opnd3)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
			    		return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Sgl_set_quiet(opnd3);
				Sgl_copytoptr(opnd3,dstptr);
				return(NOEXCEPTION);
			}
			/*
		 	 * return quiet NaN
		 	 */
			Sgl_copytoptr(opnd1,dstptr);
			return(NOEXCEPTION);
		}
	}

	/*
	 * check second operand for NaN's or infinity
	 */
	if (Sgl_isinfinity_exponent(opnd2)) {
		if (Sgl_iszero_mantissa(opnd2)) {
			if (Sgl_isnotnan(opnd3)) {
				if (Sgl_iszero_exponentmantissa(opnd1)) {
					/* 
					 * invalid since multiply operands are
					 * zero & infinity
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Sgl_makequietnan(opnd2);
					Sgl_copytoptr(opnd2,dstptr);
					return(NOEXCEPTION);
				}

				/*
				 * Check third operand for infinity with a
				 *  sign opposite of the multiply result
				 */
				if (Sgl_isinfinity(opnd3) &&
				    (Sgl_sign(resultp1) ^ Sgl_sign(opnd3))) {
					/* 
					 * invalid since attempting a magnitude
					 * subtraction of infinities
					 */
					if (Is_invalidtrap_enabled())
				       		return(OPC_2E_INVALIDEXCEPTION);
				       	Set_invalidflag();
				       	Sgl_makequietnan(resultp1);
					Sgl_copytoptr(resultp1,dstptr);
					return(NOEXCEPTION);
				}

				/*
				 * return infinity
				 */
				Sgl_setinfinity_exponentmantissa(resultp1);
				Sgl_copytoptr(resultp1,dstptr);
				return(NOEXCEPTION);
			}
		}
		else {
			/*
			 * is NaN; signaling or quiet?
			 */
			if (Sgl_isone_signaling(opnd2)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
					return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Sgl_set_quiet(opnd2);
			}
			/* 
			 * is third operand a signaling NaN? 
			 */
			else if (Sgl_is_signalingnan(opnd3)) {
			       	/* trap if INVALIDTRAP enabled */
			       	if (Is_invalidtrap_enabled())
				   		return(OPC_2E_INVALIDEXCEPTION);
			       	/* make NaN quiet */
			       	Set_invalidflag();
			       	Sgl_set_quiet(opnd3);
				Sgl_copytoptr(opnd3,dstptr);
		       		return(NOEXCEPTION);
			}
			/*
			 * return quiet NaN
			 */
			Sgl_copytoptr(opnd2,dstptr);
			return(NOEXCEPTION);
		}
	}

	/*
	 * check third operand for NaN's or infinity
	 */
	if (Sgl_isinfinity_exponent(opnd3)) {
		if (Sgl_iszero_mantissa(opnd3)) {
			/* return infinity */
			Sgl_copytoptr(opnd3,dstptr);
			return(NOEXCEPTION);
		} else {
			/*
			 * is NaN; signaling or quiet?
			 */
			if (Sgl_isone_signaling(opnd3)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
					return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Sgl_set_quiet(opnd3);
			}
			/*
			 * return quiet NaN
 			 */
			Sgl_copytoptr(opnd3,dstptr);
			return(NOEXCEPTION);
		}
    	}

	/*
	 * Generate multiply mantissa
	 */
	if (Sgl_isnotzero_exponent(opnd1)) {
		/* set hidden bit */
		Sgl_clear_signexponent_set_hidden(opnd1);
	}
	else {
		/* check for zero */
		if (Sgl_iszero_mantissa(opnd1)) {
			/*
			 * Perform the add opnd3 with zero here.
			 */
			if (Sgl_iszero_exponentmantissa(opnd3)) {
				if (Is_rounding_mode(ROUNDMINUS)) {
					Sgl_or_signs(opnd3,resultp1);
				} else {
					Sgl_and_signs(opnd3,resultp1);
				}
			}
			/*
			 * Now let's check for trapped underflow case.
			 */
			else if (Sgl_iszero_exponent(opnd3) &&
			         Is_underflowtrap_enabled()) {
                    		/* need to normalize results mantissa */
                    		sign_save = Sgl_signextendedsign(opnd3);
				result_exponent = 0;
                    		Sgl_leftshiftby1(opnd3);
                    		Sgl_normalize(opnd3,result_exponent);
                    		Sgl_set_sign(opnd3,/*using*/sign_save);
                    		Sgl_setwrapped_exponent(opnd3,result_exponent,
							unfl);
                    		Sgl_copytoptr(opnd3,dstptr);
                    		/* inexact = FALSE */
                    		return(OPC_2E_UNDERFLOWEXCEPTION);
			}
			Sgl_copytoptr(opnd3,dstptr);
			return(NOEXCEPTION);
		}
		/* is denormalized, adjust exponent */
		Sgl_clear_signexponent(opnd1);
		Sgl_leftshiftby1(opnd1);
		Sgl_normalize(opnd1,mpy_exponent);
	}
	/* opnd2 needs to have hidden bit set with msb in hidden bit */
	if (Sgl_isnotzero_exponent(opnd2)) {
		Sgl_clear_signexponent_set_hidden(opnd2);
	}
	else {
		/* check for zero */
		if (Sgl_iszero_mantissa(opnd2)) {
			/*
			 * Perform the add opnd3 with zero here.
			 */
			if (Sgl_iszero_exponentmantissa(opnd3)) {
				if (Is_rounding_mode(ROUNDMINUS)) {
					Sgl_or_signs(opnd3,resultp1);
				} else {
					Sgl_and_signs(opnd3,resultp1);
				}
			}
			/*
			 * Now let's check for trapped underflow case.
			 */
			else if (Sgl_iszero_exponent(opnd3) &&
			    Is_underflowtrap_enabled()) {
                    		/* need to normalize results mantissa */
                    		sign_save = Sgl_signextendedsign(opnd3);
				result_exponent = 0;
                    		Sgl_leftshiftby1(opnd3);
                    		Sgl_normalize(opnd3,result_exponent);
                    		Sgl_set_sign(opnd3,/*using*/sign_save);
                    		Sgl_setwrapped_exponent(opnd3,result_exponent,
							unfl);
                    		Sgl_copytoptr(opnd3,dstptr);
                    		/* inexact = FALSE */
                    		return(OPC_2E_UNDERFLOWEXCEPTION);
			}
			Sgl_copytoptr(opnd3,dstptr);
			return(NOEXCEPTION);
		}
		/* is denormalized; want to normalize */
		Sgl_clear_signexponent(opnd2);
		Sgl_leftshiftby1(opnd2);
		Sgl_normalize(opnd2,mpy_exponent);
	}

	/* Multiply the first two source mantissas together */

	/* 
	 * The intermediate result will be kept in tmpres,
	 * which needs enough room for 106 bits of mantissa,
	 * so lets call it a Double extended.
	 */
	Sglext_setzero(tmpresp1,tmpresp2);

	/* 
	 * Four bits at a time are inspected in each loop, and a 
	 * simple shift and add multiply algorithm is used. 
	 */ 
	for (count = SGL_P-1; count >= 0; count -= 4) {
		Sglext_rightshiftby4(tmpresp1,tmpresp2);
		if (Sbit28(opnd1)) {
	 		/* Twoword_add should be an ADD followed by 2 ADDC's */
			Twoword_add(tmpresp1, tmpresp2, opnd2<<3, 0);
		}
		if (Sbit29(opnd1)) {
			Twoword_add(tmpresp1, tmpresp2, opnd2<<2, 0);
		}
		if (Sbit30(opnd1)) {
			Twoword_add(tmpresp1, tmpresp2, opnd2<<1, 0);
		}
		if (Sbit31(opnd1)) {
			Twoword_add(tmpresp1, tmpresp2, opnd2, 0);
		}
		Sgl_rightshiftby4(opnd1);
	}
	if (Is_sexthiddenoverflow(tmpresp1)) {
		/* result mantissa >= 2 (mantissa overflow) */
		mpy_exponent++;
		Sglext_rightshiftby4(tmpresp1,tmpresp2);
	} else {
		Sglext_rightshiftby3(tmpresp1,tmpresp2);
	}

	/*
	 * Restore the sign of the mpy result which was saved in resultp1.
	 * The exponent will continue to be kept in mpy_exponent.
	 */
	Sglext_set_sign(tmpresp1,Sgl_sign(resultp1));

	/* 
	 * No rounding is required, since the result of the multiply
	 * is exact in the extended format.
	 */

	/*
	 * Now we are ready to perform the add portion of the operation.
	 *
	 * The exponents need to be kept as integers for now, since the
	 * multiply result might not fit into the exponent field.  We
	 * can't overflow or underflow because of this yet, since the
	 * add could bring the final result back into range.
	 */
	add_exponent = Sgl_exponent(opnd3);

	/*
	 * Check for denormalized or zero add operand.
	 */
	if (add_exponent == 0) {
		/* check for zero */
		if (Sgl_iszero_mantissa(opnd3)) {
			/* right is zero */
			/* Left can't be zero and must be result.
			 *
			 * The final result is now in tmpres and mpy_exponent,
			 * and needs to be rounded and squeezed back into
			 * double precision format from double extended.
			 */
			result_exponent = mpy_exponent;
			Sglext_copy(tmpresp1,tmpresp2,resultp1,resultp2);
			sign_save = Sgl_signextendedsign(resultp1);/*save sign*/
			goto round;
		}

		/* 
		 * Neither are zeroes.  
		 * Adjust exponent and normalize add operand.
		 */
		sign_save = Sgl_signextendedsign(opnd3);	/* save sign */
		Sgl_clear_signexponent(opnd3);
		Sgl_leftshiftby1(opnd3);
		Sgl_normalize(opnd3,add_exponent);
		Sgl_set_sign(opnd3,sign_save);		/* restore sign */
	} else {
		Sgl_clear_exponent_set_hidden(opnd3);
	}
	/*
	 * Copy opnd3 to the double extended variable called right.
	 */
	Sgl_copyto_sglext(opnd3,rightp1,rightp2);

	/*
	 * A zero "save" helps discover equal operands (for later),
	 * and is used in swapping operands (if needed).
	 */
	Sglext_xortointp1(tmpresp1,rightp1,/*to*/save);

	/*
	 * Compare magnitude of operands.
	 */
	Sglext_copytoint_exponentmantissa(tmpresp1,signlessleft1);
	Sglext_copytoint_exponentmantissa(rightp1,signlessright1);
	if (mpy_exponent < add_exponent || mpy_exponent == add_exponent &&
	    Sglext_ismagnitudeless(signlessleft1,signlessright1)) {
		/*
		 * Set the left operand to the larger one by XOR swap.
		 * First finish the first word "save".
		 */
		Sglext_xorfromintp1(save,rightp1,/*to*/rightp1);
		Sglext_xorfromintp1(save,tmpresp1,/*to*/tmpresp1);
		Sglext_swap_lower(tmpresp2,rightp2);
		/* also setup exponents used in rest of routine */
		diff_exponent = add_exponent - mpy_exponent;
		result_exponent = add_exponent;
	} else {
		/* also setup exponents used in rest of routine */
		diff_exponent = mpy_exponent - add_exponent;
		result_exponent = mpy_exponent;
	}
	/* Invariant: left is not smaller than right. */

	/*
	 * Special case alignment of operands that would force alignment
	 * beyond the extent of the extension.  A further optimization
	 * could special case this but only reduces the path length for
	 * this infrequent case.
	 */
	if (diff_exponent > SGLEXT_THRESHOLD) {
		diff_exponent = SGLEXT_THRESHOLD;
	}

	/* Align right operand by shifting it to the right */
	Sglext_clear_sign(rightp1);
	Sglext_right_align(rightp1,rightp2,/*shifted by*/diff_exponent);
	
	/* Treat sum and difference of the operands separately. */
	if ((int)save < 0) {
		/*
		 * Difference of the two operands.  Overflow can occur if the
		 * multiply overflowed.  A borrow can occur out of the hidden
		 * bit and force a post normalization phase.
		 */
		Sglext_subtract(tmpresp1,tmpresp2, rightp1,rightp2,
			resultp1,resultp2);
		sign_save = Sgl_signextendedsign(resultp1);
		if (Sgl_iszero_hidden(resultp1)) {
			/* Handle normalization */
		/* A straightforward algorithm would now shift the
		 * result and extension left until the hidden bit
		 * becomes one.  Not all of the extension bits need
		 * participate in the shift.  Only the two most 
		 * significant bits (round and guard) are needed.
		 * If only a single shift is needed then the guard
		 * bit becomes a significant low order bit and the
		 * extension must participate in the rounding.
		 * If more than a single shift is needed, then all
		 * bits to the right of the guard bit are zeros, 
		 * and the guard bit may or may not be zero. */
			Sglext_leftshiftby1(resultp1,resultp2);

			/* Need to check for a zero result.  The sign and
			 * exponent fields have already been zeroed.  The more
			 * efficient test of the full object can be used.
			 */
			 if (Sglext_iszero(resultp1,resultp2)) {
				/* Must have been "x-x" or "x+(-x)". */
				if (Is_rounding_mode(ROUNDMINUS))
					Sgl_setone_sign(resultp1);
				Sgl_copytoptr(resultp1,dstptr);
				return(NOEXCEPTION);
			}
			result_exponent--;

			/* Look to see if normalization is finished. */
			if (Sgl_isone_hidden(resultp1)) {
				/* No further normalization is needed */
				goto round;
			}

			/* Discover first one bit to determine shift amount.
			 * Use a modified binary search.  We have already
			 * shifted the result one position right and still
			 * not found a one so the remainder of the extension
			 * must be zero and simplifies rounding. */
			/* Scan bytes */
			while (Sgl_iszero_hiddenhigh7mantissa(resultp1)) {
				Sglext_leftshiftby8(resultp1,resultp2);
				result_exponent -= 8;
			}
			/* Now narrow it down to the nibble */
			if (Sgl_iszero_hiddenhigh3mantissa(resultp1)) {
				/* The lower nibble contains the
				 * normalizing one */
				Sglext_leftshiftby4(resultp1,resultp2);
				result_exponent -= 4;
			}
			/* Select case where first bit is set (already
			 * normalized) otherwise select the proper shift. */
			jumpsize = Sgl_hiddenhigh3mantissa(resultp1);
			if (jumpsize <= 7) switch(jumpsize) {
			case 1:
				Sglext_leftshiftby3(resultp1,resultp2);
				result_exponent -= 3;
				break;
			case 2:
			case 3:
				Sglext_leftshiftby2(resultp1,resultp2);
				result_exponent -= 2;
				break;
			case 4:
			case 5:
			case 6:
			case 7:
				Sglext_leftshiftby1(resultp1,resultp2);
				result_exponent -= 1;
				break;
			}
		} /* end if (hidden...)... */
	/* Fall through and round */
	} /* end if (save < 0)... */
	else {
		/* Add magnitudes */
		Sglext_addition(tmpresp1,tmpresp2,
			rightp1,rightp2, /*to*/resultp1,resultp2);
		sign_save = Sgl_signextendedsign(resultp1);
		if (Sgl_isone_hiddenoverflow(resultp1)) {
	    		/* Prenormalization required. */
	    		Sglext_arithrightshiftby1(resultp1,resultp2);
	    		result_exponent++;
		} /* end if hiddenoverflow... */
	} /* end else ...add magnitudes... */

	/* Round the result.  If the extension and lower two words are
	 * all zeros, then the result is exact.  Otherwise round in the
	 * correct direction.  Underflow is possible. If a postnormalization
	 * is necessary, then the mantissa is all zeros so no shift is needed.
	 */
  round:
	if (result_exponent <= 0 && !Is_underflowtrap_enabled()) {
		Sglext_denormalize(resultp1,resultp2,result_exponent,is_tiny);
	}
	Sgl_set_sign(resultp1,/*using*/sign_save);
	if (Sglext_isnotzero_mantissap2(resultp2)) {
		inexact = TRUE;
		switch(Rounding_mode()) {
		case ROUNDNEAREST: /* The default. */
			if (Sglext_isone_highp2(resultp2)) {
				/* at least 1/2 ulp */
				if (Sglext_isnotzero_low31p2(resultp2) ||
				    Sglext_isone_lowp1(resultp1)) {
					/* either exactly half way and odd or
					 * more than 1/2ulp */
					Sgl_increment(resultp1);
				}
			}
	    		break;

		case ROUNDPLUS:
	    		if (Sgl_iszero_sign(resultp1)) {
				/* Round up positive results */
				Sgl_increment(resultp1);
			}
			break;
	    
		case ROUNDMINUS:
	    		if (Sgl_isone_sign(resultp1)) {
				/* Round down negative results */
				Sgl_increment(resultp1);
			}
	    
		case ROUNDZERO:;
			/* truncate is simple */
		} /* end switch... */
		if (Sgl_isone_hiddenoverflow(resultp1)) result_exponent++;
	}
	if (result_exponent >= SGL_INFINITY_EXPONENT) {
		/* Overflow */
		if (Is_overflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                        Sgl_setwrapped_exponent(resultp1,result_exponent,ovfl);
                        Sgl_copytoptr(resultp1,dstptr);
                        if (inexact)
                            if (Is_inexacttrap_enabled())
                                return (OPC_2E_OVERFLOWEXCEPTION |
					OPC_2E_INEXACTEXCEPTION);
                            else Set_inexactflag();
                        return (OPC_2E_OVERFLOWEXCEPTION);
		}
		inexact = TRUE;
		Set_overflowflag();
		Sgl_setoverflow(resultp1);
	} else if (result_exponent <= 0) {	/* underflow case */
		if (Is_underflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                	Sgl_setwrapped_exponent(resultp1,result_exponent,unfl);
			Sgl_copytoptr(resultp1,dstptr);
                        if (inexact)
                            if (Is_inexacttrap_enabled())
                                return (OPC_2E_UNDERFLOWEXCEPTION |
					OPC_2E_INEXACTEXCEPTION);
                            else Set_inexactflag();
	    		return(OPC_2E_UNDERFLOWEXCEPTION);
		}
		else if (inexact && is_tiny) Set_underflowflag();
	}
	else Sgl_set_exponent(resultp1,result_exponent);
	Sgl_copytoptr(resultp1,dstptr);
	if (inexact) 
		if (Is_inexacttrap_enabled()) return(OPC_2E_INEXACTEXCEPTION);
		else Set_inexactflag();
    	return(NOEXCEPTION);
}

/*
 *  Single Floating-point Multiply Negate Fused Add
 */

sgl_fmpynfadd(src1ptr,src2ptr,src3ptr,status,dstptr)

sgl_floating_point *src1ptr, *src2ptr, *src3ptr, *dstptr;
unsigned int *status;
{
	unsigned int opnd1, opnd2, opnd3;
	register unsigned int tmpresp1, tmpresp2;
	unsigned int rightp1, rightp2;
	unsigned int resultp1, resultp2 = 0;
	register int mpy_exponent, add_exponent, count;
	boolean inexact = FALSE, is_tiny = FALSE;

	unsigned int signlessleft1, signlessright1, save;
	register int result_exponent, diff_exponent;
	int sign_save, jumpsize;
	
	Sgl_copyfromptr(src1ptr,opnd1);
	Sgl_copyfromptr(src2ptr,opnd2);
	Sgl_copyfromptr(src3ptr,opnd3);

	/* 
	 * set sign bit of result of multiply
	 */
	if (Sgl_sign(opnd1) ^ Sgl_sign(opnd2)) 
		Sgl_setzero(resultp1);
	else 
		Sgl_setnegativezero(resultp1); 

	/*
	 * Generate multiply exponent 
	 */
	mpy_exponent = Sgl_exponent(opnd1) + Sgl_exponent(opnd2) - SGL_BIAS;

	/*
	 * check first operand for NaN's or infinity
	 */
	if (Sgl_isinfinity_exponent(opnd1)) {
		if (Sgl_iszero_mantissa(opnd1)) {
			if (Sgl_isnotnan(opnd2) && Sgl_isnotnan(opnd3)) {
				if (Sgl_iszero_exponentmantissa(opnd2)) {
					/* 
					 * invalid since operands are infinity 
					 * and zero 
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Sgl_makequietnan(resultp1);
					Sgl_copytoptr(resultp1,dstptr);
					return(NOEXCEPTION);
				}
				/*
				 * Check third operand for infinity with a
				 *  sign opposite of the multiply result
				 */
				if (Sgl_isinfinity(opnd3) &&
				    (Sgl_sign(resultp1) ^ Sgl_sign(opnd3))) {
					/* 
					 * invalid since attempting a magnitude
					 * subtraction of infinities
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Sgl_makequietnan(resultp1);
					Sgl_copytoptr(resultp1,dstptr);
					return(NOEXCEPTION);
				}

				/*
			 	 * return infinity
			 	 */
				Sgl_setinfinity_exponentmantissa(resultp1);
				Sgl_copytoptr(resultp1,dstptr);
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
			    		return(OPC_2E_INVALIDEXCEPTION);
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
			    		return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Sgl_set_quiet(opnd2);
				Sgl_copytoptr(opnd2,dstptr);
				return(NOEXCEPTION);
			}
			/* 
			 * is third operand a signaling NaN? 
			 */
			else if (Sgl_is_signalingnan(opnd3)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
			    		return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Sgl_set_quiet(opnd3);
				Sgl_copytoptr(opnd3,dstptr);
				return(NOEXCEPTION);
			}
			/*
		 	 * return quiet NaN
		 	 */
			Sgl_copytoptr(opnd1,dstptr);
			return(NOEXCEPTION);
		}
	}

	/*
	 * check second operand for NaN's or infinity
	 */
	if (Sgl_isinfinity_exponent(opnd2)) {
		if (Sgl_iszero_mantissa(opnd2)) {
			if (Sgl_isnotnan(opnd3)) {
				if (Sgl_iszero_exponentmantissa(opnd1)) {
					/* 
					 * invalid since multiply operands are
					 * zero & infinity
					 */
					if (Is_invalidtrap_enabled())
						return(OPC_2E_INVALIDEXCEPTION);
					Set_invalidflag();
					Sgl_makequietnan(opnd2);
					Sgl_copytoptr(opnd2,dstptr);
					return(NOEXCEPTION);
				}

				/*
				 * Check third operand for infinity with a
				 *  sign opposite of the multiply result
				 */
				if (Sgl_isinfinity(opnd3) &&
				    (Sgl_sign(resultp1) ^ Sgl_sign(opnd3))) {
					/* 
					 * invalid since attempting a magnitude
					 * subtraction of infinities
					 */
					if (Is_invalidtrap_enabled())
				       		return(OPC_2E_INVALIDEXCEPTION);
				       	Set_invalidflag();
				       	Sgl_makequietnan(resultp1);
					Sgl_copytoptr(resultp1,dstptr);
					return(NOEXCEPTION);
				}

				/*
				 * return infinity
				 */
				Sgl_setinfinity_exponentmantissa(resultp1);
				Sgl_copytoptr(resultp1,dstptr);
				return(NOEXCEPTION);
			}
		}
		else {
			/*
			 * is NaN; signaling or quiet?
			 */
			if (Sgl_isone_signaling(opnd2)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
					return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Sgl_set_quiet(opnd2);
			}
			/* 
			 * is third operand a signaling NaN? 
			 */
			else if (Sgl_is_signalingnan(opnd3)) {
			       	/* trap if INVALIDTRAP enabled */
			       	if (Is_invalidtrap_enabled())
				   		return(OPC_2E_INVALIDEXCEPTION);
			       	/* make NaN quiet */
			       	Set_invalidflag();
			       	Sgl_set_quiet(opnd3);
				Sgl_copytoptr(opnd3,dstptr);
		       		return(NOEXCEPTION);
			}
			/*
			 * return quiet NaN
			 */
			Sgl_copytoptr(opnd2,dstptr);
			return(NOEXCEPTION);
		}
	}

	/*
	 * check third operand for NaN's or infinity
	 */
	if (Sgl_isinfinity_exponent(opnd3)) {
		if (Sgl_iszero_mantissa(opnd3)) {
			/* return infinity */
			Sgl_copytoptr(opnd3,dstptr);
			return(NOEXCEPTION);
		} else {
			/*
			 * is NaN; signaling or quiet?
			 */
			if (Sgl_isone_signaling(opnd3)) {
				/* trap if INVALIDTRAP enabled */
				if (Is_invalidtrap_enabled())
					return(OPC_2E_INVALIDEXCEPTION);
				/* make NaN quiet */
				Set_invalidflag();
				Sgl_set_quiet(opnd3);
			}
			/*
			 * return quiet NaN
 			 */
			Sgl_copytoptr(opnd3,dstptr);
			return(NOEXCEPTION);
		}
    	}

	/*
	 * Generate multiply mantissa
	 */
	if (Sgl_isnotzero_exponent(opnd1)) {
		/* set hidden bit */
		Sgl_clear_signexponent_set_hidden(opnd1);
	}
	else {
		/* check for zero */
		if (Sgl_iszero_mantissa(opnd1)) {
			/*
			 * Perform the add opnd3 with zero here.
			 */
			if (Sgl_iszero_exponentmantissa(opnd3)) {
				if (Is_rounding_mode(ROUNDMINUS)) {
					Sgl_or_signs(opnd3,resultp1);
				} else {
					Sgl_and_signs(opnd3,resultp1);
				}
			}
			/*
			 * Now let's check for trapped underflow case.
			 */
			else if (Sgl_iszero_exponent(opnd3) &&
			         Is_underflowtrap_enabled()) {
                    		/* need to normalize results mantissa */
                    		sign_save = Sgl_signextendedsign(opnd3);
				result_exponent = 0;
                    		Sgl_leftshiftby1(opnd3);
                    		Sgl_normalize(opnd3,result_exponent);
                    		Sgl_set_sign(opnd3,/*using*/sign_save);
                    		Sgl_setwrapped_exponent(opnd3,result_exponent,
							unfl);
                    		Sgl_copytoptr(opnd3,dstptr);
                    		/* inexact = FALSE */
                    		return(OPC_2E_UNDERFLOWEXCEPTION);
			}
			Sgl_copytoptr(opnd3,dstptr);
			return(NOEXCEPTION);
		}
		/* is denormalized, adjust exponent */
		Sgl_clear_signexponent(opnd1);
		Sgl_leftshiftby1(opnd1);
		Sgl_normalize(opnd1,mpy_exponent);
	}
	/* opnd2 needs to have hidden bit set with msb in hidden bit */
	if (Sgl_isnotzero_exponent(opnd2)) {
		Sgl_clear_signexponent_set_hidden(opnd2);
	}
	else {
		/* check for zero */
		if (Sgl_iszero_mantissa(opnd2)) {
			/*
			 * Perform the add opnd3 with zero here.
			 */
			if (Sgl_iszero_exponentmantissa(opnd3)) {
				if (Is_rounding_mode(ROUNDMINUS)) {
					Sgl_or_signs(opnd3,resultp1);
				} else {
					Sgl_and_signs(opnd3,resultp1);
				}
			}
			/*
			 * Now let's check for trapped underflow case.
			 */
			else if (Sgl_iszero_exponent(opnd3) &&
			    Is_underflowtrap_enabled()) {
                    		/* need to normalize results mantissa */
                    		sign_save = Sgl_signextendedsign(opnd3);
				result_exponent = 0;
                    		Sgl_leftshiftby1(opnd3);
                    		Sgl_normalize(opnd3,result_exponent);
                    		Sgl_set_sign(opnd3,/*using*/sign_save);
                    		Sgl_setwrapped_exponent(opnd3,result_exponent,
							unfl);
                    		Sgl_copytoptr(opnd3,dstptr);
                    		/* inexact = FALSE */
                    		return(OPC_2E_UNDERFLOWEXCEPTION);
			}
			Sgl_copytoptr(opnd3,dstptr);
			return(NOEXCEPTION);
		}
		/* is denormalized; want to normalize */
		Sgl_clear_signexponent(opnd2);
		Sgl_leftshiftby1(opnd2);
		Sgl_normalize(opnd2,mpy_exponent);
	}

	/* Multiply the first two source mantissas together */

	/* 
	 * The intermediate result will be kept in tmpres,
	 * which needs enough room for 106 bits of mantissa,
	 * so lets call it a Double extended.
	 */
	Sglext_setzero(tmpresp1,tmpresp2);

	/* 
	 * Four bits at a time are inspected in each loop, and a 
	 * simple shift and add multiply algorithm is used. 
	 */ 
	for (count = SGL_P-1; count >= 0; count -= 4) {
		Sglext_rightshiftby4(tmpresp1,tmpresp2);
		if (Sbit28(opnd1)) {
	 		/* Twoword_add should be an ADD followed by 2 ADDC's */
			Twoword_add(tmpresp1, tmpresp2, opnd2<<3, 0);
		}
		if (Sbit29(opnd1)) {
			Twoword_add(tmpresp1, tmpresp2, opnd2<<2, 0);
		}
		if (Sbit30(opnd1)) {
			Twoword_add(tmpresp1, tmpresp2, opnd2<<1, 0);
		}
		if (Sbit31(opnd1)) {
			Twoword_add(tmpresp1, tmpresp2, opnd2, 0);
		}
		Sgl_rightshiftby4(opnd1);
	}
	if (Is_sexthiddenoverflow(tmpresp1)) {
		/* result mantissa >= 2 (mantissa overflow) */
		mpy_exponent++;
		Sglext_rightshiftby4(tmpresp1,tmpresp2);
	} else {
		Sglext_rightshiftby3(tmpresp1,tmpresp2);
	}

	/*
	 * Restore the sign of the mpy result which was saved in resultp1.
	 * The exponent will continue to be kept in mpy_exponent.
	 */
	Sglext_set_sign(tmpresp1,Sgl_sign(resultp1));

	/* 
	 * No rounding is required, since the result of the multiply
	 * is exact in the extended format.
	 */

	/*
	 * Now we are ready to perform the add portion of the operation.
	 *
	 * The exponents need to be kept as integers for now, since the
	 * multiply result might not fit into the exponent field.  We
	 * can't overflow or underflow because of this yet, since the
	 * add could bring the final result back into range.
	 */
	add_exponent = Sgl_exponent(opnd3);

	/*
	 * Check for denormalized or zero add operand.
	 */
	if (add_exponent == 0) {
		/* check for zero */
		if (Sgl_iszero_mantissa(opnd3)) {
			/* right is zero */
			/* Left can't be zero and must be result.
			 *
			 * The final result is now in tmpres and mpy_exponent,
			 * and needs to be rounded and squeezed back into
			 * double precision format from double extended.
			 */
			result_exponent = mpy_exponent;
			Sglext_copy(tmpresp1,tmpresp2,resultp1,resultp2);
			sign_save = Sgl_signextendedsign(resultp1);/*save sign*/
			goto round;
		}

		/* 
		 * Neither are zeroes.  
		 * Adjust exponent and normalize add operand.
		 */
		sign_save = Sgl_signextendedsign(opnd3);	/* save sign */
		Sgl_clear_signexponent(opnd3);
		Sgl_leftshiftby1(opnd3);
		Sgl_normalize(opnd3,add_exponent);
		Sgl_set_sign(opnd3,sign_save);		/* restore sign */
	} else {
		Sgl_clear_exponent_set_hidden(opnd3);
	}
	/*
	 * Copy opnd3 to the double extended variable called right.
	 */
	Sgl_copyto_sglext(opnd3,rightp1,rightp2);

	/*
	 * A zero "save" helps discover equal operands (for later),
	 * and is used in swapping operands (if needed).
	 */
	Sglext_xortointp1(tmpresp1,rightp1,/*to*/save);

	/*
	 * Compare magnitude of operands.
	 */
	Sglext_copytoint_exponentmantissa(tmpresp1,signlessleft1);
	Sglext_copytoint_exponentmantissa(rightp1,signlessright1);
	if (mpy_exponent < add_exponent || mpy_exponent == add_exponent &&
	    Sglext_ismagnitudeless(signlessleft1,signlessright1)) {
		/*
		 * Set the left operand to the larger one by XOR swap.
		 * First finish the first word "save".
		 */
		Sglext_xorfromintp1(save,rightp1,/*to*/rightp1);
		Sglext_xorfromintp1(save,tmpresp1,/*to*/tmpresp1);
		Sglext_swap_lower(tmpresp2,rightp2);
		/* also setup exponents used in rest of routine */
		diff_exponent = add_exponent - mpy_exponent;
		result_exponent = add_exponent;
	} else {
		/* also setup exponents used in rest of routine */
		diff_exponent = mpy_exponent - add_exponent;
		result_exponent = mpy_exponent;
	}
	/* Invariant: left is not smaller than right. */

	/*
	 * Special case alignment of operands that would force alignment
	 * beyond the extent of the extension.  A further optimization
	 * could special case this but only reduces the path length for
	 * this infrequent case.
	 */
	if (diff_exponent > SGLEXT_THRESHOLD) {
		diff_exponent = SGLEXT_THRESHOLD;
	}

	/* Align right operand by shifting it to the right */
	Sglext_clear_sign(rightp1);
	Sglext_right_align(rightp1,rightp2,/*shifted by*/diff_exponent);
	
	/* Treat sum and difference of the operands separately. */
	if ((int)save < 0) {
		/*
		 * Difference of the two operands.  Overflow can occur if the
		 * multiply overflowed.  A borrow can occur out of the hidden
		 * bit and force a post normalization phase.
		 */
		Sglext_subtract(tmpresp1,tmpresp2, rightp1,rightp2,
			resultp1,resultp2);
		sign_save = Sgl_signextendedsign(resultp1);
		if (Sgl_iszero_hidden(resultp1)) {
			/* Handle normalization */
		/* A straightforward algorithm would now shift the
		 * result and extension left until the hidden bit
		 * becomes one.  Not all of the extension bits need
		 * participate in the shift.  Only the two most 
		 * significant bits (round and guard) are needed.
		 * If only a single shift is needed then the guard
		 * bit becomes a significant low order bit and the
		 * extension must participate in the rounding.
		 * If more than a single shift is needed, then all
		 * bits to the right of the guard bit are zeros, 
		 * and the guard bit may or may not be zero. */
			Sglext_leftshiftby1(resultp1,resultp2);

			/* Need to check for a zero result.  The sign and
			 * exponent fields have already been zeroed.  The more
			 * efficient test of the full object can be used.
			 */
			 if (Sglext_iszero(resultp1,resultp2)) {
				/* Must have been "x-x" or "x+(-x)". */
				if (Is_rounding_mode(ROUNDMINUS))
					Sgl_setone_sign(resultp1);
				Sgl_copytoptr(resultp1,dstptr);
				return(NOEXCEPTION);
			}
			result_exponent--;

			/* Look to see if normalization is finished. */
			if (Sgl_isone_hidden(resultp1)) {
				/* No further normalization is needed */
				goto round;
			}

			/* Discover first one bit to determine shift amount.
			 * Use a modified binary search.  We have already
			 * shifted the result one position right and still
			 * not found a one so the remainder of the extension
			 * must be zero and simplifies rounding. */
			/* Scan bytes */
			while (Sgl_iszero_hiddenhigh7mantissa(resultp1)) {
				Sglext_leftshiftby8(resultp1,resultp2);
				result_exponent -= 8;
			}
			/* Now narrow it down to the nibble */
			if (Sgl_iszero_hiddenhigh3mantissa(resultp1)) {
				/* The lower nibble contains the
				 * normalizing one */
				Sglext_leftshiftby4(resultp1,resultp2);
				result_exponent -= 4;
			}
			/* Select case where first bit is set (already
			 * normalized) otherwise select the proper shift. */
			jumpsize = Sgl_hiddenhigh3mantissa(resultp1);
			if (jumpsize <= 7) switch(jumpsize) {
			case 1:
				Sglext_leftshiftby3(resultp1,resultp2);
				result_exponent -= 3;
				break;
			case 2:
			case 3:
				Sglext_leftshiftby2(resultp1,resultp2);
				result_exponent -= 2;
				break;
			case 4:
			case 5:
			case 6:
			case 7:
				Sglext_leftshiftby1(resultp1,resultp2);
				result_exponent -= 1;
				break;
			}
		} /* end if (hidden...)... */
	/* Fall through and round */
	} /* end if (save < 0)... */
	else {
		/* Add magnitudes */
		Sglext_addition(tmpresp1,tmpresp2,
			rightp1,rightp2, /*to*/resultp1,resultp2);
		sign_save = Sgl_signextendedsign(resultp1);
		if (Sgl_isone_hiddenoverflow(resultp1)) {
	    		/* Prenormalization required. */
	    		Sglext_arithrightshiftby1(resultp1,resultp2);
	    		result_exponent++;
		} /* end if hiddenoverflow... */
	} /* end else ...add magnitudes... */

	/* Round the result.  If the extension and lower two words are
	 * all zeros, then the result is exact.  Otherwise round in the
	 * correct direction.  Underflow is possible. If a postnormalization
	 * is necessary, then the mantissa is all zeros so no shift is needed.
	 */
  round:
	if (result_exponent <= 0 && !Is_underflowtrap_enabled()) {
		Sglext_denormalize(resultp1,resultp2,result_exponent,is_tiny);
	}
	Sgl_set_sign(resultp1,/*using*/sign_save);
	if (Sglext_isnotzero_mantissap2(resultp2)) {
		inexact = TRUE;
		switch(Rounding_mode()) {
		case ROUNDNEAREST: /* The default. */
			if (Sglext_isone_highp2(resultp2)) {
				/* at least 1/2 ulp */
				if (Sglext_isnotzero_low31p2(resultp2) ||
				    Sglext_isone_lowp1(resultp1)) {
					/* either exactly half way and odd or
					 * more than 1/2ulp */
					Sgl_increment(resultp1);
				}
			}
	    		break;

		case ROUNDPLUS:
	    		if (Sgl_iszero_sign(resultp1)) {
				/* Round up positive results */
				Sgl_increment(resultp1);
			}
			break;
	    
		case ROUNDMINUS:
	    		if (Sgl_isone_sign(resultp1)) {
				/* Round down negative results */
				Sgl_increment(resultp1);
			}
	    
		case ROUNDZERO:;
			/* truncate is simple */
		} /* end switch... */
		if (Sgl_isone_hiddenoverflow(resultp1)) result_exponent++;
	}
	if (result_exponent >= SGL_INFINITY_EXPONENT) {
		/* Overflow */
		if (Is_overflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                        Sgl_setwrapped_exponent(resultp1,result_exponent,ovfl);
                        Sgl_copytoptr(resultp1,dstptr);
                        if (inexact)
                            if (Is_inexacttrap_enabled())
                                return (OPC_2E_OVERFLOWEXCEPTION |
					OPC_2E_INEXACTEXCEPTION);
                            else Set_inexactflag();
                        return (OPC_2E_OVERFLOWEXCEPTION);
		}
		inexact = TRUE;
		Set_overflowflag();
		Sgl_setoverflow(resultp1);
	} else if (result_exponent <= 0) {	/* underflow case */
		if (Is_underflowtrap_enabled()) {
                        /*
                         * Adjust bias of result
                         */
                	Sgl_setwrapped_exponent(resultp1,result_exponent,unfl);
			Sgl_copytoptr(resultp1,dstptr);
                        if (inexact)
                            if (Is_inexacttrap_enabled())
                                return (OPC_2E_UNDERFLOWEXCEPTION |
					OPC_2E_INEXACTEXCEPTION);
                            else Set_inexactflag();
	    		return(OPC_2E_UNDERFLOWEXCEPTION);
		}
		else if (inexact && is_tiny) Set_underflowflag();
	}
	else Sgl_set_exponent(resultp1,result_exponent);
	Sgl_copytoptr(resultp1,dstptr);
	if (inexact) 
		if (Is_inexacttrap_enabled()) return(OPC_2E_INEXACTEXCEPTION);
		else Set_inexactflag();
    	return(NOEXCEPTION);
}

