/* Decimal 32-bit format module header for the decNumber C Library
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

#if !defined(DECIMAL32)
#define DECIMAL32
#define DEC32NAME     "decimal32"	/* Short name */
#define DEC32FULLNAME "Decimal 32-bit Number"	/* Verbose name */
#define DEC32AUTHOR   "Mike Cowlishaw"	/* Who to blame */

  /* parameters for decimal32s */
#define DECIMAL32_Bytes  4	/* length */
#define DECIMAL32_Pmax   7	/* maximum precision (digits) */
#define DECIMAL32_Emax   96	/* maximum adjusted exponent */
#define DECIMAL32_Emin  -95	/* minimum adjusted exponent */
#define DECIMAL32_Bias   101	/* bias for the exponent */
#define DECIMAL32_String 15	/* maximum string length, +1 */
  /* highest biased exponent (Elimit-1) */
#define DECIMAL32_Ehigh  (DECIMAL32_Emax+DECIMAL32_Bias-DECIMAL32_Pmax+1)

#ifndef DECNUMDIGITS
#define DECNUMDIGITS DECIMAL32_Pmax	/* size if not already defined */
#endif
#ifndef DECNUMBER
#include "decNumber.h"		/* context and number library */
#endif

  /* Decimal 32-bit type, accessible by bytes */
typedef struct
{
  uint8_t bytes[DECIMAL32_Bytes];	/* decimal32: 1, 5, 6, 20 bits */
} decimal32;

  /* special values [top byte excluding sign bit; last two bits are
     don't-care for Infinity on input, last bit don't-care for NaN] */
#if !defined(DECIMAL_NaN)
#define DECIMAL_NaN     0x7c	/* 0 11111 00 NaN */
#define DECIMAL_sNaN    0x7e	/* 0 11111 10 sNaN */
#define DECIMAL_Inf     0x78	/* 0 11110 00 Infinity */
#endif

  /* Macros for accessing decimal32 fields.  These assume the argument
     is a reference (pointer) to the decimal32 structure */
  /* Get sign */
#define decimal32Sign(d)       ((unsigned)(d)->bytes[0]>>7)

  /* Get combination field */
#define decimal32Comb(d)       (((d)->bytes[0] & 0x7c)>>2)

  /* Get exponent continuation [does not remove bias] */
#define decimal32ExpCon(d)     ((((d)->bytes[0] & 0x03)<<4)         \
                               | ((unsigned)(d)->bytes[1]>>4))

  /* Set sign [this assumes sign previously 0] */
#define decimal32SetSign(d, b) {                                    \
    (d)->bytes[0]|=((unsigned)(b)<<7);}

  /* Set exponent continuation [does not apply bias] */
  /* This assumes range has been checked and exponent previously 0; */
  /* type of exponent must be unsigned */
#define decimal32SetExpCon(d, e) {                                  \
    (d)->bytes[0]|=(uint8_t)((e)>>4);                                 \
    (d)->bytes[1]|=(uint8_t)(((e)&0x0F)<<4);}

  /* ------------------------------------------------------------------ */
  /* Routines                                                           */
  /* ------------------------------------------------------------------ */

#ifdef IN_LIBGCC2
#define decimal32FromString __decimal32FromString
#define decimal32ToString __decimal32ToString
#define decimal32ToEngString __decimal32ToEngString
#define decimal32FromNumber __decimal32FromNumber
#define decimal32ToNumber __decimal32ToNumber
#endif

/* String conversions.  */
decimal32 *decimal32FromString (decimal32 *, const char *, decContext *);
char *decimal32ToString (const decimal32 *, char *);
char *decimal32ToEngString (const decimal32 *, char *);

/* decNumber conversions.  */
decimal32 *decimal32FromNumber (decimal32 *, const decNumber *, decContext *);
decNumber *decimal32ToNumber (const decimal32 *, decNumber *);

#endif
