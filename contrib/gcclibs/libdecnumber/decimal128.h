/* Decimal 128-bit format module header for the decNumber C Library
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

#if !defined(DECIMAL128)
#define DECIMAL128
#define DEC128NAME     "decimal128"	/* Short name */
#define DEC128FULLNAME "Decimal 128-bit Number"	/* Verbose name */
#define DEC128AUTHOR   "Mike Cowlishaw"	/* Who to blame */

#if defined(DECIMAL32)
#error decimal128.h must precede decimal32.h for correct DECNUMDIGITS
#else
#if defined(DECIMAL64)
#error decimal128.h must precede decimal64.h for correct DECNUMDIGITS
#endif
#endif

  /* parameters for decimal128s */
#define DECIMAL128_Bytes  16	/* length */
#define DECIMAL128_Pmax   34	/* maximum precision (digits) */
#define DECIMAL128_Emax   6144	/* maximum adjusted exponent */
#define DECIMAL128_Emin  -6143	/* minimum adjusted exponent */
#define DECIMAL128_Bias   6176	/* bias for the exponent */
#define DECIMAL128_String 43	/* maximum string length, +1 */
  /* highest biased exponent (Elimit-1) */
#define DECIMAL128_Ehigh  (DECIMAL128_Emax+DECIMAL128_Bias-DECIMAL128_Pmax+1)

#ifndef DECNUMDIGITS
#define DECNUMDIGITS DECIMAL128_Pmax	/* size if not already defined */
#endif
#ifndef DECNUMBER
#include "decNumber.h"		/* context and number library */
#endif

  /* Decimal 128-bit type, accessible by bytes */
typedef struct
{
  uint8_t bytes[DECIMAL128_Bytes];	/* decimal128: 1, 5, 12, 110 bits */
} decimal128;

  /* special values [top byte excluding sign bit; last two bits are
     don't-care for Infinity on input, last bit don't-care for NaN] */
#if !defined(DECIMAL_NaN)
#define DECIMAL_NaN     0x7c	/* 0 11111 00 NaN */
#define DECIMAL_sNaN    0x7e	/* 0 11111 10 sNaN */
#define DECIMAL_Inf     0x78	/* 0 11110 00 Infinity */
#endif

  /* Macros for accessing decimal128 fields.  These assume the argument
     is a reference (pointer) to the decimal128 structure */
  /* Get sign */
#define decimal128Sign(d)       ((unsigned)(d)->bytes[0]>>7)

  /* Get combination field */
#define decimal128Comb(d)       (((d)->bytes[0] & 0x7c)>>2)

  /* Get exponent continuation [does not remove bias] */
#define decimal128ExpCon(d)     ((((d)->bytes[0] & 0x03)<<10)       \
                                | ((unsigned)(d)->bytes[1]<<2)        \
                                | ((unsigned)(d)->bytes[2]>>6))

  /* Set sign [this assumes sign previously 0] */
#define decimal128SetSign(d, b) {                                   \
    (d)->bytes[0]|=((unsigned)(b)<<7);}

  /* Set exponent continuation [does not apply bias] */
  /* This assumes range has been checked and exponent previously 0; */
  /* type of exponent must be unsigned */
#define decimal128SetExpCon(d, e) {                                 \
    (d)->bytes[0]|=(uint8_t)((e)>>10);                                \
    (d)->bytes[1] =(uint8_t)(((e)&0x3fc)>>2);                         \
    (d)->bytes[2]|=(uint8_t)(((e)&0x03)<<6);}

  /* ------------------------------------------------------------------ */
  /* Routines                                                           */
  /* ------------------------------------------------------------------ */

#ifdef IN_LIBGCC2
#define decimal128FromString __decimal128FromString
#define decimal128ToString __decimal128ToString
#define decimal128ToEngString __decimal128ToEngString
#define decimal128FromNumber __decimal128FromNumber
#define decimal128ToNumber __decimal128ToNumber
#endif

  /* String conversions */
decimal128 *decimal128FromString (decimal128 *, const char *, decContext *);
char *decimal128ToString (const decimal128 *, char *);
char *decimal128ToEngString (const decimal128 *, char *);

  /* decNumber conversions */
decimal128 *decimal128FromNumber (decimal128 *, const decNumber *, decContext *);
decNumber *decimal128ToNumber (const decimal128 *, decNumber *);

#endif
