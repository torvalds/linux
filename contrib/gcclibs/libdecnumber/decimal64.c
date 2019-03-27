/* Decimal 64-bit format module for the decNumber C Library
   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by IBM Corporation.  Author Mike Cowlishaw.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2, or (at your option) any later
   version.

   In addition to the permissions in the GNU General Public License,
   the Free Software Foundation gives you unlimited permission to link
   the compiled version of this file into combinations with other
   programs, and to distribute those combinations without any
   restriction coming from the use of this file.  (The General Public
   License restrictions do apply in other respects; for example, they
   cover modification of the file, and distribution when not linked
   into a combine executable.)

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* ------------------------------------------------------------------ */
/* This module comprises the routines for decimal64 format numbers.   */
/* Conversions are supplied to and from decNumber and String.         */
/*                                                                    */
/* No arithmetic routines are included; decNumber provides these.     */
/*                                                                    */
/* Error handling is the same as decNumber (qv.).                     */
/* ------------------------------------------------------------------ */
#include <string.h>		/* [for memset/memcpy] */
#include <stdio.h>		/* [for printf] */

#define  DECNUMDIGITS 16	/* we need decNumbers with space for 16 */
#include "config.h"
#include "decNumber.h"		/* base number library */
#include "decNumberLocal.h"	/* decNumber local types, etc. */
#include "decimal64.h"		/* our primary include */
#include "decUtility.h"		/* utility routines */

#if DECTRACE || DECCHECK
void decimal64Show (const decimal64 *);	/* for debug */
void decNumberShow (const decNumber *);	/* .. */
#endif

/* Useful macro */
/* Clear a structure (e.g., a decNumber) */
#define DEC_clear(d) memset(d, 0, sizeof(*d))

/* ------------------------------------------------------------------ */
/* decimal64FromNumber -- convert decNumber to decimal64              */
/*                                                                    */
/*   ds is the target decimal64                                       */
/*   dn is the source number (assumed valid)                          */
/*   set is the context, used only for reporting errors               */
/*                                                                    */
/* The set argument is used only for status reporting and for the     */
/* rounding mode (used if the coefficient is more than DECIMAL64_Pmax */
/* digits or an overflow is detected).  If the exponent is out of the */
/* valid range then Overflow or Underflow will be raised.             */
/* After Underflow a subnormal result is possible.                    */
/*                                                                    */
/* DEC_Clamped is set if the number has to be 'folded down' to fit,   */
/* by reducing its exponent and multiplying the coefficient by a      */
/* power of ten, or if the exponent on a zero had to be clamped.      */
/* ------------------------------------------------------------------ */
decimal64 *
decimal64FromNumber (decimal64 * d64, const decNumber * dn, decContext * set)
{
  uInt status = 0;		/* status accumulator */
  Int pad = 0;			/* coefficient pad digits */
  decNumber dw;			/* work */
  decContext dc;		/* .. */
  uByte isneg = dn->bits & DECNEG;	/* non-0 if original sign set */
  uInt comb, exp;		/* work */

  /* If the number is finite, and has too many digits, or the exponent */
  /* could be out of range then we reduce the number under the */
  /* appropriate constraints */
  if (!(dn->bits & DECSPECIAL))
    {				/* not a special value */
      Int ae = dn->exponent + dn->digits - 1;	/* adjusted exponent */
      if (dn->digits > DECIMAL64_Pmax	/* too many digits */
	  || ae > DECIMAL64_Emax	/* likely overflow */
	  || ae < DECIMAL64_Emin)
	{			/* likely underflow */
	  decContextDefault (&dc, DEC_INIT_DECIMAL64);	/* [no traps] */
	  dc.round = set->round;	/* use supplied rounding */
	  decNumberPlus (&dw, dn, &dc);	/* (round and check) */
	  /* [this changes -0 to 0, but it will be restored below] */
	  status |= dc.status;	/* save status */
	  dn = &dw;		/* use the work number */
	}
      /* [this could have pushed number to Infinity or zero, so this */
      /* rounding must be done before we generate the decimal64] */
    }

  DEC_clear (d64);		/* clean the target */
  if (dn->bits & DECSPECIAL)
    {				/* a special value */
      uByte top;		/* work */
      if (dn->bits & DECINF)
	top = DECIMAL_Inf;
      else
	{			/* sNaN or qNaN */
	  if ((*dn->lsu != 0 || dn->digits > 1)	/* non-zero coefficient */
	      && (dn->digits < DECIMAL64_Pmax))
	    {			/* coefficient fits */
	      decDensePackCoeff (dn, d64->bytes, sizeof (d64->bytes), 0);
	    }
	  if (dn->bits & DECNAN)
	    top = DECIMAL_NaN;
	  else
	    top = DECIMAL_sNaN;
	}
      d64->bytes[0] = top;
    }
  else if (decNumberIsZero (dn))
    {				/* a zero */
      /* set and clamp exponent */
      if (dn->exponent < -DECIMAL64_Bias)
	{
	  exp = 0;
	  status |= DEC_Clamped;
	}
      else
	{
	  exp = dn->exponent + DECIMAL64_Bias;	/* bias exponent */
	  if (exp > DECIMAL64_Ehigh)
	    {			/* top clamp */
	      exp = DECIMAL64_Ehigh;
	      status |= DEC_Clamped;
	    }
	}
      comb = (exp >> 5) & 0x18;	/* combination field */
      d64->bytes[0] = (uByte) (comb << 2);
      exp &= 0xff;		/* remaining exponent bits */
      decimal64SetExpCon (d64, exp);
    }
  else
    {				/* non-zero finite number */
      uInt msd;			/* work */

      /* we have a dn that fits, but it may need to be padded */
      exp = (uInt) (dn->exponent + DECIMAL64_Bias);	/* bias exponent */
      if (exp > DECIMAL64_Ehigh)
	{			/* fold-down case */
	  pad = exp - DECIMAL64_Ehigh;
	  exp = DECIMAL64_Ehigh;	/* [to maximum] */
	  status |= DEC_Clamped;
	}

      decDensePackCoeff (dn, d64->bytes, sizeof (d64->bytes), pad);

      /* save and clear the top digit */
      msd = ((unsigned) d64->bytes[1] >> 2) & 0x0f;
      d64->bytes[1] &= 0x03;
      /* create the combination field */
      if (msd >= 8)
	comb = 0x18 | (msd & 0x01) | ((exp >> 7) & 0x06);
      else
	comb = (msd & 0x07) | ((exp >> 5) & 0x18);
      d64->bytes[0] = (uByte) (comb << 2);
      exp &= 0xff;		/* remaining exponent bits */
      decimal64SetExpCon (d64, exp);
    }

  if (isneg)
    decimal64SetSign (d64, 1);
  if (status != 0)
    decContextSetStatus (set, status);	/* pass on status */

  /*decimal64Show(d64); */
  return d64;
}

/* ------------------------------------------------------------------ */
/* decimal64ToNumber -- convert decimal64 to decNumber                */
/*   d64 is the source decimal64                                      */
/*   dn is the target number, with appropriate space                  */
/* No error is possible.                                              */
/* ------------------------------------------------------------------ */
decNumber *
decimal64ToNumber (const decimal64 * d64, decNumber * dn)
{
  uInt msd;			/* coefficient MSD */
  decimal64 wk;			/* working copy, if needed */
  uInt top = d64->bytes[0] & 0x7f;	/* top byte, less sign bit */
  decNumberZero (dn);		/* clean target */
  /* set the sign if negative */
  if (decimal64Sign (d64))
    dn->bits = DECNEG;

  if (top >= 0x78)
    {				/* is a special */
      if ((top & 0x7c) == (DECIMAL_Inf & 0x7c))
	dn->bits |= DECINF;
      else if ((top & 0x7e) == (DECIMAL_NaN & 0x7e))
	dn->bits |= DECNAN;
      else
	dn->bits |= DECSNAN;
      msd = 0;			/* no top digit */
    }
  else
    {				/* have a finite number */
      uInt comb = top >> 2;	/* combination field */
      uInt exp;			/* exponent */

      if (comb >= 0x18)
	{
	  msd = 8 + (comb & 0x01);
	  exp = (comb & 0x06) << 7;	/* MSBs */
	}
      else
	{
	  msd = comb & 0x07;
	  exp = (comb & 0x18) << 5;
	}
      dn->exponent = exp + decimal64ExpCon (d64) - DECIMAL64_Bias;	/* remove bias */
    }

  /* get the coefficient, unless infinite */
  if (!(dn->bits & DECINF))
    {
      Int bunches = DECIMAL64_Pmax / 3;	/* coefficient full bunches to convert */
      Int odd = 0;		/* assume MSD is 0 (no odd bunch) */
      if (msd != 0)
	{			/* coefficient has leading non-0 digit */
	  /* make a copy of the decimal64, with an extra bunch which has */
	  /* the top digit ready for conversion */
	  wk = *d64;		/* take a copy */
	  wk.bytes[0] = 0;	/* clear all but coecon */
	  wk.bytes[1] &= 0x03;	/* .. */
	  wk.bytes[1] |= (msd << 2);	/* and prefix MSD */
	  odd++;		/* indicate the extra */
	  d64 = &wk;		/* use the work copy */
	}
      decDenseUnpackCoeff (d64->bytes, sizeof (d64->bytes), dn, bunches, odd);
    }
  return dn;
}

/* ------------------------------------------------------------------ */
/* to-scientific-string -- conversion to numeric string               */
/* to-engineering-string -- conversion to numeric string              */
/*                                                                    */
/*   decimal64ToString(d64, string);                                  */
/*   decimal64ToEngString(d64, string);                               */
/*                                                                    */
/*  d64 is the decimal64 format number to convert                     */
/*  string is the string where the result will be laid out            */
/*                                                                    */
/*  string must be at least 24 characters                             */
/*                                                                    */
/*  No error is possible, and no status can be set.                   */
/* ------------------------------------------------------------------ */
char *
decimal64ToString (const decimal64 * d64, char *string)
{
  decNumber dn;			/* work */
  decimal64ToNumber (d64, &dn);
  decNumberToString (&dn, string);
  return string;
}

char *
decimal64ToEngString (const decimal64 * d64, char *string)
{
  decNumber dn;			/* work */
  decimal64ToNumber (d64, &dn);
  decNumberToEngString (&dn, string);
  return string;
}

/* ------------------------------------------------------------------ */
/* to-number -- conversion from numeric string                        */
/*                                                                    */
/*   decimal64FromString(result, string, set);                        */
/*                                                                    */
/*  result  is the decimal64 format number which gets the result of   */
/*          the conversion                                            */
/*  *string is the character string which should contain a valid      */
/*          number (which may be a special value)                     */
/*  set     is the context                                            */
/*                                                                    */
/* The context is supplied to this routine is used for error handling */
/* (setting of status and traps) and for the rounding mode, only.     */
/* If an error occurs, the result will be a valid decimal64 NaN.      */
/* ------------------------------------------------------------------ */
decimal64 *
decimal64FromString (decimal64 * result, const char *string, decContext * set)
{
  decContext dc;		/* work */
  decNumber dn;			/* .. */

  decContextDefault (&dc, DEC_INIT_DECIMAL64);	/* no traps, please */
  dc.round = set->round;	/* use supplied rounding */

  decNumberFromString (&dn, string, &dc);	/* will round if needed */

  decimal64FromNumber (result, &dn, &dc);
  if (dc.status != 0)
    {				/* something happened */
      decContextSetStatus (set, dc.status);	/* .. pass it on */
    }
  return result;
}

#if DECTRACE || DECCHECK
/* ------------------------------------------------------------------ */
/* decimal64Show -- display a single in hexadecimal [debug aid]       */
/*   d64 -- the number to show                                        */
/* ------------------------------------------------------------------ */
/* Also shows sign/cob/expconfields extracted */
void
decimal64Show (const decimal64 * d64)
{
  char buf[DECIMAL64_Bytes * 2 + 1];
  Int i, j;
  j = 0;
  for (i = 0; i < DECIMAL64_Bytes; i++)
    {
      sprintf (&buf[j], "%02x", d64->bytes[i]);
      j = j + 2;
    }
  printf (" D64> %s [S:%d Cb:%02x E:%d]\n", buf,
	  decimal64Sign (d64), decimal64Comb (d64), decimal64ExpCon (d64));
}
#endif
