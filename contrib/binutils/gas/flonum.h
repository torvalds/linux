/* flonum.h - Floating point package
   Copyright 1987, 1990, 1991, 1992, 1994, 1996, 2000, 2003
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/***********************************************************************\
 *									*
 *	Arbitrary-precision floating point arithmetic.			*
 *									*
 *									*
 *	Notation: a floating point number is expressed as		*
 *	MANTISSA * (2 ** EXPONENT).					*
 *									*
 *	If this offends more traditional mathematicians, then		*
 *	please tell me your nomenclature for flonums!			*
 *									*
 \***********************************************************************/

#include "bignum.h"

/***********************************************************************\
 *									*
 *	Variable precision floating point numbers.			*
 *									*
 *	Exponent is the place value of the low littlenum. E.g.:		*
 *	If  0:  low points to the units             littlenum.		*
 *	If  1:  low points to the LITTLENUM_RADIX   littlenum.		*
 *	If -1:  low points to the 1/LITTLENUM_RADIX littlenum.		*
 *									*
 \***********************************************************************/

/* JF:  A sign value of 0 means we have been asked to assemble NaN
   A sign value of 'P' means we've been asked to assemble +Inf
   A sign value of 'N' means we've been asked to assemble -Inf
   */
struct FLONUM_STRUCT {
  LITTLENUM_TYPE *low;		/* low order littlenum of a bignum */
  LITTLENUM_TYPE *high;		/* high order littlenum of a bignum */
  LITTLENUM_TYPE *leader;	/* -> 1st non-zero littlenum */
  /* If flonum is 0.0, leader==low-1 */
  long exponent;		/* base LITTLENUM_RADIX */
  char sign;			/* '+' or '-' */
};

typedef struct FLONUM_STRUCT FLONUM_TYPE;

/***********************************************************************\
 *									*
 *	Since we can (& do) meet with exponents like 10^5000, it	*
 *	is silly to make a table of ~ 10,000 entries, one for each	*
 *	power of 10. We keep a table where item [n] is a struct		*
 *	FLONUM_FLOATING_POINT representing 10^(2^n). We then		*
 *	multiply appropriate entries from this table to get any		*
 *	particular power of 10. For the example of 10^5000, a table	*
 *	of just 25 entries suffices: 10^(2^-12)...10^(2^+12).		*
 *									*
 \***********************************************************************/

extern const FLONUM_TYPE flonum_positive_powers_of_ten[];
extern const FLONUM_TYPE flonum_negative_powers_of_ten[];
extern const int table_size_of_flonum_powers_of_ten;
/* Flonum_XXX_powers_of_ten[] table has legal indices from 0 to
   + this number inclusive.  */

/***********************************************************************\
 *									*
 *	Declare worker functions.					*
 *									*
 \***********************************************************************/

int atof_generic (char **address_of_string_pointer,
		  const char *string_of_decimal_marks,
		  const char *string_of_decimal_exponent_marks,
		  FLONUM_TYPE * address_of_generic_floating_point_number);

void flonum_copy (FLONUM_TYPE * in, FLONUM_TYPE * out);
void flonum_multip (const FLONUM_TYPE * a, const FLONUM_TYPE * b,
		    FLONUM_TYPE * product);

/***********************************************************************\
 *									*
 *	Declare error codes.						*
 *									*
 \***********************************************************************/

#define ERROR_EXPONENT_OVERFLOW (2)
