/* Decimal Number module header for the decNumber C Library
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

#if !defined(DECNUMBER)
#define DECNUMBER
#define DECNAME     "decNumber"	/* Short name */
#define DECVERSION  "decNumber 3.24"	/* Version [16 max.] */
#define DECFULLNAME "Decimal Number Module"	/* Verbose name */
#define DECAUTHOR   "Mike Cowlishaw"	/* Who to blame */

#if !defined(DECCONTEXT)
#include "decContext.h"
#endif


  /* Bit settings for decNumber.bits */
#define DECNEG    0x80		/* Sign; 1=negative, 0=positive or zero */
#define DECINF    0x40		/* 1=Infinity */
#define DECNAN    0x20		/* 1=NaN */
#define DECSNAN   0x10		/* 1=sNaN */
  /* The remaining bits are reserved; they must be 0 */
#define DECSPECIAL (DECINF|DECNAN|DECSNAN)	/* any special value */

  /* DECNUMDIGITS is the default number of digits we can hold in the */
  /* structure.  If undefined, 1 is assumed and it is assumed that the */
  /* structure will be immediately followed by extra space (if */
  /* required).  DECNUMDIGITS is always >0. */
#if !defined(DECNUMDIGITS)
#define DECNUMDIGITS 1
#endif


  /* Define the decNumber data structure.  The size and shape of the */
  /* units array in the structure is determined by the following */
  /* constant.  This must not be changed without recompiling the */
  /* decNumber library modules. */
#define DECDPUN 4		/* Decimal Digits Per UNit [must be in */
				   /* range 1-9; power of 2 recommended]. */
  /* The size (integer data type) of each unit is determined by the */
  /* number of digits it will hold. */
#if   DECDPUN<=2
#define decNumberUnit uint8_t
#elif DECDPUN<=4
#define decNumberUnit uint16_t
#else
#define decNumberUnit uint32_t
#endif
  /* The number of decNumberUnits we need is ceiling of DECNUMDIGITS/DECDPUN */
#define DECNUMUNITS ((DECNUMDIGITS+DECDPUN-1)/DECDPUN)

  /* The data structure... */
typedef struct
{
  int32_t digits;		/* Count of digits in the coefficient; >0 */
  int32_t exponent;		/* Unadjusted exponent, unbiased, in */
  /* range: -1999999997 through 999999999 */
  uint8_t bits;			/* Indicator bits (see above) */
  decNumberUnit lsu[DECNUMUNITS];	/* Coefficient, from least significant unit */
} decNumber;

  /* Notes: */
  /* 1. If digits is > DECDPUN then there will be more than one */
  /*    decNumberUnits immediately following the first element of lsu. */
  /*    These contain the remaining (more significant) digits of the */
  /*    number, and may be in the lsu array, or may be guaranteed by */
  /*    some other mechanism (such as being contained in another */
  /*    structure, or being overlaid on dynamically allocated storage). */
  /* */
  /*    Each integer of the coefficient (except the possibly the last) */
  /*    contains DECDPUN digits (e.g., a value in the range 0 through */
  /*    99999999 if DECDPUN is 8, or 0 through 9999 if DECDPUN is 4). */
  /* */
  /* 2. A decNumber converted to a string may need up to digits+14 */
  /*    characters.  The worst cases (non-exponential and exponential */
  /*    formats) are: -0.00000{9...}# */
  /*             and: -9.{9...}E+999999999#   (where # is '\0') */


  /* ------------------------------------------------------------------ */
  /* decNumber public functions and macros                              */
  /* ------------------------------------------------------------------ */

#ifdef IN_LIBGCC2
#define decNumberFromString __decNumberFromString
#define decNumberToString __decNumberToString
#define decNumberToEngString __decNumberToEngString
#define decNumberAbs __decNumberAbs
#define decNumberAdd __decNumberAdd
#define decNumberCompare __decNumberCompare
#define decNumberDivide __decNumberDivide
#define decNumberDivideInteger __decNumberDivideInteger
#define decNumberMax __decNumberMax
#define decNumberMin __decNumberMin
#define decNumberMinus __decNumberMinus
#define decNumberMultiply __decNumberMultiply
#define decNumberNormalize __decNumberNormalize
#define decNumberPlus __decNumberPlus
#define decNumberPower __decNumberPower
#define decNumberQuantize __decNumberQuantize
#define decNumberRemainder __decNumberRemainder
#define decNumberRemainderNear __decNumberRemainderNear
#define decNumberRescale __decNumberRescale
#define decNumberSameQuantum __decNumberSameQuantum
#define decNumberSquareRoot __decNumberSquareRoot
#define decNumberSubtract __decNumberSubtract
#define decNumberToIntegralValue __decNumberToIntegralValue
#define decNumberCopy __decNumberCopy
#define decNumberTrim __decNumberTrim
#define decNumberVersion __decNumberVersion
#define decNumberZero __decNumberZero
#endif

  /* Conversions */
decNumber *decNumberFromString (decNumber *, const char *, decContext *);
char *decNumberToString (const decNumber *, char *);
char *decNumberToEngString (const decNumber *, char *);

  /* Operators */
decNumber *decNumberAbs (decNumber *, const decNumber *, decContext *);
decNumber *decNumberAdd (decNumber *, const decNumber *,
			 const decNumber *, decContext *);
decNumber *decNumberCompare (decNumber *, const decNumber *,
			     const decNumber *, decContext *);
decNumber *decNumberDivide (decNumber *, const decNumber *,
			    const decNumber *, decContext *);
decNumber *decNumberDivideInteger (decNumber *, const decNumber *,
				   const decNumber *, decContext *);
decNumber *decNumberMax (decNumber *, const decNumber *,
			 const decNumber *, decContext *);
decNumber *decNumberMin (decNumber *, const decNumber *,
			 const decNumber *, decContext *);
decNumber *decNumberMinus (decNumber *, const decNumber *, decContext *);
decNumber *decNumberMultiply (decNumber *, const decNumber *,
			      const decNumber *, decContext *);
decNumber *decNumberNormalize (decNumber *, const decNumber *, decContext *);
decNumber *decNumberPlus (decNumber *, const decNumber *, decContext *);
decNumber *decNumberPower (decNumber *, const decNumber *,
			   const decNumber *, decContext *);
decNumber *decNumberQuantize (decNumber *, const decNumber *,
			      const decNumber *, decContext *);
decNumber *decNumberRemainder (decNumber *, const decNumber *,
			       const decNumber *, decContext *);
decNumber *decNumberRemainderNear (decNumber *, const decNumber *,
				   const decNumber *, decContext *);
decNumber *decNumberRescale (decNumber *, const decNumber *,
			     const decNumber *, decContext *);
decNumber *decNumberSameQuantum (decNumber *, const decNumber *, const decNumber *);
decNumber *decNumberSquareRoot (decNumber *, const decNumber *, decContext *);
decNumber *decNumberSubtract (decNumber *, const decNumber *,
			      const decNumber *, decContext *);
decNumber *decNumberToIntegralValue (decNumber *, const decNumber *, decContext *);

  /* Utilities */
decNumber *decNumberCopy (decNumber *, const decNumber *);
decNumber *decNumberTrim (decNumber *);
const char *decNumberVersion (void);
decNumber *decNumberZero (decNumber *);

  /* Macros */
#define decNumberIsZero(dn)     (*(dn)->lsu==0 \
                                   && (dn)->digits==1 \
                                   && (((dn)->bits&DECSPECIAL)==0))
#define decNumberIsNegative(dn) (((dn)->bits&DECNEG)!=0)
#define decNumberIsNaN(dn)      (((dn)->bits&(DECNAN|DECSNAN))!=0)
#define decNumberIsInfinite(dn) (((dn)->bits&DECINF)!=0)

#endif
