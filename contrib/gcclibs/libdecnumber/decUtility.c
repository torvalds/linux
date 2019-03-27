/* Utility functions for decimal floating point support via decNumber.
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

#include "config.h"
#include "decNumber.h"          /* base number library */
#include "decNumberLocal.h"     /* decNumber local types, etc. */
#include "decUtility.h"         /* utility routines */

/* ================================================================== */
/* Shared utility routines                                            */
/* ================================================================== */

/* define and include the conversion tables to use */
#define DEC_BIN2DPD 1		/* used for all sizes */
#if DECDPUN==3
#define DEC_DPD2BIN 1
#else
#define DEC_DPD2BCD 1
#endif
#include "decDPD.h"		/* lookup tables */

/* The maximum number of decNumberUnits we need for a working copy of */
/* the units array is the ceiling of digits/DECDPUN, where digits is */
/* the maximum number of digits in any of the formats for which this */
/* is used.  We do not want to include decimal128.h, so, as a very */
/* special case, that number is defined here. */
#define DECMAX754   34
#define DECMAXUNITS ((DECMAX754+DECDPUN-1)/DECDPUN)

/* ------------------------------------------------------------------ */
/* decDensePackCoeff -- densely pack coefficient into DPD form        */
/*                                                                    */
/*   dn is the source number (assumed valid, max DECMAX754 digits)    */
/*   bytes is the target's byte array                                 */
/*   len is length of target format's byte array                      */
/*   shift is the number of 0 digits to add on the right (normally 0) */
/*                                                                    */
/* The coefficient must be known small enough to fit, and is filled   */
/* in from the right (least significant first).  Note that the full   */
/* coefficient is copied, including the leading 'odd' digit.  This    */
/* digit is retrieved and packed into the combination field by the    */
/* caller.                                                            */
/*                                                                    */
/* shift is used for 'fold-down' padding.                             */
/*                                                                    */
/* No error is possible.                                              */
/* ------------------------------------------------------------------ */
void
decDensePackCoeff (const decNumber * dn, uByte * bytes, Int len, Int shift)
{
  Int cut;			/* work */
  Int n;			/* output bunch counter */
  Int digits = dn->digits;	/* digit countdown */
  uInt dpd;			/* densely packed decimal value */
  uInt bin;			/* binary value 0-999 */
  uByte *bout;			/* -> current output byte */
  const Unit *inu = dn->lsu;	/* -> current input unit */
  Unit uar[DECMAXUNITS];	/* working copy of units, iff shifted */
#if DECDPUN!=3			/* not fast path */
  Unit in;			/* current input unit */
#endif

  if (shift != 0)
    {				/* shift towards most significant required */
      /* shift the units array to the left by pad digits and copy */
      /* [this code is a special case of decShiftToMost, which could */
      /* be used instead if exposed and the array were copied first] */
      Unit *target, *first;	/* work */
      const Unit *source;	/* work */
      uInt next = 0;		/* work */

      source = dn->lsu + D2U (digits) - 1;	/* where msu comes from */
      first = uar + D2U (digits + shift) - 1;	/* where msu will end up */
      target = uar + D2U (digits) - 1 + D2U (shift);	/* where upper part of first cut goes */

      cut = (DECDPUN - shift % DECDPUN) % DECDPUN;
      for (; source >= dn->lsu; source--, target--)
	{
	  /* split the source Unit and accumulate remainder for next */
	  uInt rem = *source % powers[cut];
	  next += *source / powers[cut];
	  if (target <= first)
	    *target = (Unit) next;	/* write to target iff valid */
	  next = rem * powers[DECDPUN - cut];	/* save remainder for next Unit */
	}
      /* propagate remainder to one below and clear the rest */
      for (; target >= uar; target--)
	{
	  *target = (Unit) next;
	  next = 0;
	}
      digits += shift;		/* add count (shift) of zeros added */
      inu = uar;		/* use units in working array */
    }

  /* densely pack the coefficient into the byte array, starting from
     the right (optionally padded) */
  bout = &bytes[len - 1];	/* rightmost result byte for phase */

#if DECDPUN!=3			/* not fast path */
  in = *inu;			/* prime */
  cut = 0;			/* at lowest digit */
  bin = 0;			/* [keep compiler quiet] */
#endif

  for (n = 0; digits > 0; n++)
    {				/* each output bunch */
#if DECDPUN==3			/* fast path, 3-at-a-time */
      bin = *inu;		/* 3 ready for convert */
      digits -= 3;		/* [may go negative] */
      inu++;			/* may need another */

#else /* must collect digit-by-digit */
      Unit dig;			/* current digit */
      Int j;			/* digit-in-bunch count */
      for (j = 0; j < 3; j++)
	{
#if DECDPUN<=4
	  Unit temp = (Unit) ((uInt) (in * 6554) >> 16);
	  dig = (Unit) (in - X10 (temp));
	  in = temp;
#else
	  dig = in % 10;
	  in = in / 10;
#endif

	  if (j == 0)
	    bin = dig;
	  else if (j == 1)
	    bin += X10 (dig);
	  else			/* j==2 */
	    bin += X100 (dig);

	  digits--;
	  if (digits == 0)
	    break;		/* [also protects *inu below] */
	  cut++;
	  if (cut == DECDPUN)
	    {
	      inu++;
	      in = *inu;
	      cut = 0;
	    }
	}
#endif
      /* here we have 3 digits in bin, or have used all input digits */

      dpd = BIN2DPD[bin];

      /* write bunch (bcd) to byte array */
      switch (n & 0x03)
	{			/* phase 0-3 */
	case 0:
	  *bout = (uByte) dpd;	/* [top 2 bits truncated] */
	  bout--;
	  *bout = (uByte) (dpd >> 8);
	  break;
	case 1:
	  *bout |= (uByte) (dpd << 2);
	  bout--;
	  *bout = (uByte) (dpd >> 6);
	  break;
	case 2:
	  *bout |= (uByte) (dpd << 4);
	  bout--;
	  *bout = (uByte) (dpd >> 4);
	  break;
	case 3:
	  *bout |= (uByte) (dpd << 6);
	  bout--;
	  *bout = (uByte) (dpd >> 2);
	  bout--;
	  break;
	}			/* switch */
    }				/* n bunches */
  return;
}

/* ------------------------------------------------------------------ */
/* decDenseUnpackCoeff -- unpack a format's coefficient               */
/*                                                                    */
/*   byte is the source's byte array                                  */
/*   len is length of the source's byte array                         */
/*   dn is the target number, with 7, 16, or 34-digit space.          */
/*   bunches is the count of DPD groups in the decNumber (2, 5, or 11)*/
/*   odd is 1 if there is a non-zero leading 10-bit group containing  */
/*     a single digit, 0 otherwise                                    */
/*                                                                    */
/* (This routine works on a copy of the number, if necessary, where   */
/* an extra 10-bit group is prefixed to the coefficient continuation  */
/* to hold the most significant digit if the latter is non-0.)        */
/*                                                                    */
/* dn->digits is set, but not the sign or exponent.                   */
/* No error is possible [the redundant 888 codes are allowed].        */
/* ------------------------------------------------------------------ */
void
decDenseUnpackCoeff (const uByte * bytes, Int len, decNumber * dn,
		     Int bunches, Int odd)
{
  uInt dpd = 0;			/* collector for 10 bits */
  Int n;			/* counter */
  const uByte *bin;		/* -> current input byte */
  Unit *uout = dn->lsu;		/* -> current output unit */
  Unit out = 0;			/* accumulator */
  Int cut = 0;			/* power of ten in current unit */
  Unit *last = uout;		/* will be unit containing msd */
#if DECDPUN!=3
  uInt bcd;			/* BCD result */
  uInt nibble;			/* work */
#endif

  /* Expand the densely-packed integer, right to left */
  bin = &bytes[len - 1];	/* next input byte to use */
  for (n = 0; n < bunches + odd; n++)
    {				/* N bunches of 10 bits */
      /* assemble the 10 bits */
      switch (n & 0x03)
	{			/* phase 0-3 */
	case 0:
	  dpd = *bin;
	  bin--;
	  dpd |= (*bin & 0x03) << 8;
	  break;
	case 1:
	  dpd = (unsigned) *bin >> 2;
	  bin--;
	  dpd |= (*bin & 0x0F) << 6;
	  break;
	case 2:
	  dpd = (unsigned) *bin >> 4;
	  bin--;
	  dpd |= (*bin & 0x3F) << 4;
	  break;
	case 3:
	  dpd = (unsigned) *bin >> 6;
	  bin--;
	  dpd |= (*bin) << 2;
	  bin--;
	  break;
	}			/*switch */

#if DECDPUN==3
      if (dpd == 0)
	*uout = 0;
      else
	{
	  *uout = DPD2BIN[dpd];	/* convert 10 bits to binary 0-999 */
	  last = uout;		/* record most significant unit */
	}
      uout++;

#else /* DECDPUN!=3 */
      if (dpd == 0)
	{			/* fastpath [e.g., leading zeros] */
	  cut += 3;
	  for (; cut >= DECDPUN;)
	    {
	      cut -= DECDPUN;
	      *uout = out;
	      uout++;
	      out = 0;
	    }
	  continue;
	}
      bcd = DPD2BCD[dpd];	/* convert 10 bits to 12 bits BCD */
      /* now split the 3 BCD nibbles into bytes, and accumulate into units */
      /* If this is the last bunch and it is an odd one, we only have one */
      /* nibble to handle [extras could overflow a Unit] */
      nibble = bcd & 0x000f;
      if (nibble)
	{
	  last = uout;
	  out = (Unit) (out + nibble * powers[cut]);
	}
      cut++;
      if (cut == DECDPUN)
	{
	  *uout = out;
	  uout++;
	  cut = 0;
	  out = 0;
	}
      if (n < bunches)
	{
	  nibble = bcd & 0x00f0;
	  if (nibble)
	    {
	      nibble >>= 4;
	      last = uout;
	      out = (Unit) (out + nibble * powers[cut]);
	    }
	  cut++;
	  if (cut == DECDPUN)
	    {
	      *uout = out;
	      uout++;
	      cut = 0;
	      out = 0;
	    }
	  nibble = bcd & 0x0f00;
	  if (nibble)
	    {
	      nibble >>= 8;
	      last = uout;
	      out = (Unit) (out + nibble * powers[cut]);
	    }
	  cut++;
	  if (cut == DECDPUN)
	    {
	      *uout = out;
	      uout++;
	      cut = 0;
	      out = 0;
	    }
	}
#endif
    }				/* n */
  if (cut != 0)
    *uout = out;		/* write out final unit */

  /* here, last points to the most significant unit with digits */
  /* we need to inspect it to get final digits count */
  dn->digits = (last - dn->lsu) * DECDPUN;	/* floor of digits */
  for (cut = 0; cut < DECDPUN; cut++)
    {
      if (*last < powers[cut])
	break;
      dn->digits++;
    }
  if (dn->digits == 0)
    dn->digits++;		/* zero has one digit */
  return;
}
