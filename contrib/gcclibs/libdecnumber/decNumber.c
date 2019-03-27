/* Decimal Number module for the decNumber C Library
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
/* This module comprises the routines for Standard Decimal Arithmetic */
/* as defined in the specification which may be found on the          */
/* http://www2.hursley.ibm.com/decimal web pages.  It implements both */
/* the full ('extended') arithmetic and the simpler ('subset')        */
/* arithmetic.                                                        */
/*                                                                    */
/* Usage notes:                                                       */
/*                                                                    */
/* 1. This code is ANSI C89 except:                                   */
/*                                                                    */
/*    a) Line comments (double forward slash) are used.  (Most C      */
/*       compilers accept these.  If yours does not, a simple script  */
/*       can be used to convert them to ANSI C comments.)             */
/*                                                                    */
/*    b) Types from C99 stdint.h are used.  If you do not have this   */
/*       header file, see the User's Guide section of the decNumber   */
/*       documentation; this lists the necessary definitions.         */
/*                                                                    */
/*    c) If DECDPUN>4, non-ANSI 64-bit 'long long' types are used.    */
/*       To avoid these, set DECDPUN <= 4 (see documentation).        */
/*                                                                    */
/* 2. The decNumber format which this library uses is optimized for   */
/*    efficient processing of relatively short numbers; in particular */
/*    it allows the use of fixed sized structures and minimizes copy  */
/*    and move operations.  It does, however, support arbitrary       */
/*    precision (up to 999,999,999 digits) and arbitrary exponent     */
/*    range (Emax in the range 0 through 999,999,999 and Emin in the  */
/*    range -999,999,999 through 0).                                  */
/*                                                                    */
/* 3. Operands to operator functions are never modified unless they   */
/*    are also specified to be the result number (which is always     */
/*    permitted).  Other than that case, operands may not overlap.    */
/*                                                                    */
/* 4. Error handling: the type of the error is ORed into the status   */
/*    flags in the current context (decContext structure).  The       */
/*    SIGFPE signal is then raised if the corresponding trap-enabler  */
/*    flag in the decContext is set (is 1).                           */
/*                                                                    */
/*    It is the responsibility of the caller to clear the status      */
/*    flags as required.                                              */
/*                                                                    */
/*    The result of any routine which returns a number will always    */
/*    be a valid number (which may be a special value, such as an     */
/*    Infinity or NaN).                                               */
/*                                                                    */
/* 5. The decNumber format is not an exchangeable concrete            */
/*    representation as it comprises fields which may be machine-     */
/*    dependent (big-endian or little-endian, for example).           */
/*    Canonical conversions to and from strings are provided; other   */
/*    conversions are available in separate modules.                  */
/*                                                                    */
/* 6. Normally, input operands are assumed to be valid.  Set DECCHECK */
/*    to 1 for extended operand checking (including NULL operands).   */
/*    Results are undefined if a badly-formed structure (or a NULL    */
/*    NULL pointer to a structure) is provided, though with DECCHECK  */
/*    enabled the operator routines are protected against exceptions. */
/*    (Except if the result pointer is NULL, which is unrecoverable.) */
/*                                                                    */
/*    However, the routines will never cause exceptions if they are   */
/*    given well-formed operands, even if the value of the operands   */
/*    is inappropriate for the operation and DECCHECK is not set.     */
/*                                                                    */
/* 7. Subset arithmetic is available only if DECSUBSET is set to 1.   */
/* ------------------------------------------------------------------ */
/* Implementation notes for maintenance of this module:               */
/*                                                                    */
/* 1. Storage leak protection:  Routines which use malloc are not     */
/*    permitted to use return for fastpath or error exits (i.e.,      */
/*    they follow strict structured programming conventions).         */
/*    Instead they have a do{}while(0); construct surrounding the     */
/*    code which is protected -- break may be used from this.         */
/*    Other routines are allowed to use the return statement inline.  */
/*                                                                    */
/*    Storage leak accounting can be enabled using DECALLOC.          */
/*                                                                    */
/* 2. All loops use the for(;;) construct.  Any do construct is for   */
/*    protection as just described.                                   */
/*                                                                    */
/* 3. Setting status in the context must always be the very last      */
/*    action in a routine, as non-0 status may raise a trap and hence */
/*    the call to set status may not return (if the handler uses long */
/*    jump).  Therefore all cleanup must be done first.  In general,  */
/*    to achieve this we accumulate status and only finally apply it  */
/*    by calling decContextSetStatus (via decStatus).                 */
/*                                                                    */
/*    Routines which allocate storage cannot, therefore, use the      */
/*    'top level' routines which could cause a non-returning          */
/*    transfer of control.  The decXxxxOp routines are safe (do not   */
/*    call decStatus even if traps are set in the context) and should */
/*    be used instead (they are also a little faster).                */
/*                                                                    */
/* 4. Exponent checking is minimized by allowing the exponent to      */
/*    grow outside its limits during calculations, provided that      */
/*    the decFinalize function is called later.  Multiplication and   */
/*    division, and intermediate calculations in exponentiation,      */
/*    require more careful checks because of the risk of 31-bit       */
/*    overflow (the most negative valid exponent is -1999999997, for  */
/*    a 999999999-digit number with adjusted exponent of -999999999). */
/*                                                                    */
/* 5. Rounding is deferred until finalization of results, with any    */
/*    'off to the right' data being represented as a single digit     */
/*    residue (in the range -1 through 9).  This avoids any double-   */
/*    rounding when more than one shortening takes place (for         */
/*    example, when a result is subnormal).                           */
/*                                                                    */
/* 6. The digits count is allowed to rise to a multiple of DECDPUN    */
/*    during many operations, so whole Units are handled and exact    */
/*    accounting of digits is not needed.  The correct digits value   */
/*    is found by decGetDigits, which accounts for leading zeros.     */
/*    This must be called before any rounding if the number of digits */
/*    is not known exactly.                                           */
/*                                                                    */
/* 7. We use the multiply-by-reciprocal 'trick' for partitioning      */
/*    numbers up to four digits, using appropriate constants.  This   */
/*    is not useful for longer numbers because overflow of 32 bits    */
/*    would lead to 4 multiplies, which is almost as expensive as     */
/*    a divide (unless we assumed floating-point multiply available). */
/*                                                                    */
/* 8. Unusual abbreviations possibly used in the commentary:          */
/*      lhs -- left hand side (operand, of an operation)              */
/*      lsd -- least significant digit (of coefficient)               */
/*      lsu -- least significant Unit (of coefficient)                */
/*      msd -- most significant digit (of coefficient)                */
/*      msu -- most significant Unit (of coefficient)                 */
/*      rhs -- right hand side (operand, of an operation)             */
/*      +ve -- positive                                               */
/*      -ve -- negative                                               */
/* ------------------------------------------------------------------ */

/* Some of glibc's string inlines cause warnings.  Plus we'd rather
   rely on (and therefore test) GCC's string builtins.  */
#define __NO_STRING_INLINES

#include <stdlib.h>		/* for malloc, free, etc. */
#include <stdio.h>		/* for printf [if needed] */
#include <string.h>		/* for strcpy */
#include <ctype.h>		/* for lower */
#include "config.h"
#include "decNumber.h"		/* base number library */
#include "decNumberLocal.h"	/* decNumber local types, etc. */

/* Constants */
/* Public constant array: powers of ten (powers[n]==10**n) */
const uInt powers[] = { 1, 10, 100, 1000, 10000, 100000, 1000000,
  10000000, 100000000, 1000000000
};

/* Local constants */
#define DIVIDE    0x80		/* Divide operators */
#define REMAINDER 0x40		/* .. */
#define DIVIDEINT 0x20		/* .. */
#define REMNEAR   0x10		/* .. */
#define COMPARE   0x01		/* Compare operators */
#define COMPMAX   0x02		/* .. */
#define COMPMIN   0x03		/* .. */
#define COMPNAN   0x04		/* .. [NaN processing] */

#define DEC_sNaN 0x40000000	/* local status: sNaN signal */
#define BADINT (Int)0x80000000	/* most-negative Int; error indicator */

static Unit one[] = { 1 };	/* Unit array of 1, used for incrementing */

/* Granularity-dependent code */
#if DECDPUN<=4
#define eInt  Int		/* extended integer */
#define ueInt uInt		/* unsigned extended integer */
  /* Constant multipliers for divide-by-power-of five using reciprocal */
  /* multiply, after removing powers of 2 by shifting, and final shift */
  /* of 17 [we only need up to **4] */
static const uInt multies[] = { 131073, 26215, 5243, 1049, 210 };

  /* QUOT10 -- macro to return the quotient of unit u divided by 10**n */
#define QUOT10(u, n) ((((uInt)(u)>>(n))*multies[n])>>17)
#else
  /* For DECDPUN>4 we currently use non-ANSI 64-bit types.  These could */
  /* be replaced by subroutine calls later. */
#ifdef long
#undef long
#endif
typedef signed long long Long;
typedef unsigned long long uLong;
#define eInt  Long		/* extended integer */
#define ueInt uLong		/* unsigned extended integer */
#endif

/* Local routines */
static decNumber *decAddOp (decNumber *, const decNumber *,
			    const decNumber *, decContext *,
			    uByte, uInt *);
static void decApplyRound (decNumber *, decContext *, Int, uInt *);
static Int decCompare (const decNumber * lhs, const decNumber * rhs);
static decNumber *decCompareOp (decNumber *, const decNumber *, const decNumber *,
				decContext *, Flag, uInt *);
static void decCopyFit (decNumber *, const decNumber *, decContext *,
			Int *, uInt *);
static decNumber *decDivideOp (decNumber *, const decNumber *, const decNumber *,
			       decContext *, Flag, uInt *);
static void decFinalize (decNumber *, decContext *, Int *, uInt *);
static Int decGetDigits (const Unit *, Int);
#if DECSUBSET
static Int decGetInt (const decNumber *, decContext *);
#else
static Int decGetInt (const decNumber *);
#endif
static decNumber *decMultiplyOp (decNumber *, const decNumber *,
				 const decNumber *, decContext *, uInt *);
static decNumber *decNaNs (decNumber *, const decNumber *, const decNumber *, uInt *);
static decNumber *decQuantizeOp (decNumber *, const decNumber *,
				 const decNumber *, decContext *, Flag, uInt *);
static void decSetCoeff (decNumber *, decContext *, const Unit *,
			 Int, Int *, uInt *);
static void decSetOverflow (decNumber *, decContext *, uInt *);
static void decSetSubnormal (decNumber *, decContext *, Int *, uInt *);
static Int decShiftToLeast (Unit *, Int, Int);
static Int decShiftToMost (Unit *, Int, Int);
static void decStatus (decNumber *, uInt, decContext *);
static Flag decStrEq (const char *, const char *);
static void decToString (const decNumber *, char[], Flag);
static decNumber *decTrim (decNumber *, Flag, Int *);
static Int decUnitAddSub (const Unit *, Int, const Unit *, Int, Int, Unit *, Int);
static Int decUnitCompare (const Unit *, Int, const Unit *, Int, Int);

#if !DECSUBSET
/* decFinish == decFinalize when no subset arithmetic needed */
#define decFinish(a,b,c,d) decFinalize(a,b,c,d)
#else
static void decFinish (decNumber *, decContext *, Int *, uInt *);
static decNumber *decRoundOperand (const decNumber *, decContext *, uInt *);
#endif

/* Diagnostic macros, etc. */
#if DECALLOC
/* Handle malloc/free accounting.  If enabled, our accountable routines */
/* are used; otherwise the code just goes straight to the system malloc */
/* and free routines. */
#define malloc(a) decMalloc(a)
#define free(a) decFree(a)
#define DECFENCE 0x5a		/* corruption detector */
/* 'Our' malloc and free: */
static void *decMalloc (size_t);
static void decFree (void *);
uInt decAllocBytes = 0;		/* count of bytes allocated */
/* Note that DECALLOC code only checks for storage buffer overflow. */
/* To check for memory leaks, the decAllocBytes variable should be */
/* checked to be 0 at appropriate times (e.g., after the test */
/* harness completes a set of tests).  This checking may be unreliable */
/* if the testing is done in a multi-thread environment. */
#endif

#if DECCHECK
/* Optional operand checking routines.  Enabling these means that */
/* decNumber and decContext operands to operator routines are checked */
/* for correctness.  This roughly doubles the execution time of the */
/* fastest routines (and adds 600+ bytes), so should not normally be */
/* used in 'production'. */
#define DECUNUSED (void *)(0xffffffff)
static Flag decCheckOperands (decNumber *, const decNumber *,
			      const decNumber *, decContext *);
static Flag decCheckNumber (const decNumber *, decContext *);
#endif

#if DECTRACE || DECCHECK
/* Optional trace/debugging routines. */
void decNumberShow (const decNumber *);	/* displays the components of a number */
static void decDumpAr (char, const Unit *, Int);
#endif

/* ================================================================== */
/* Conversions                                                        */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* to-scientific-string -- conversion to numeric string               */
/* to-engineering-string -- conversion to numeric string              */
/*                                                                    */
/*   decNumberToString(dn, string);                                   */
/*   decNumberToEngString(dn, string);                                */
/*                                                                    */
/*  dn is the decNumber to convert                                    */
/*  string is the string where the result will be laid out            */
/*                                                                    */
/*  string must be at least dn->digits+14 characters long             */
/*                                                                    */
/*  No error is possible, and no status can be set.                   */
/* ------------------------------------------------------------------ */
char *
decNumberToString (const decNumber * dn, char *string)
{
  decToString (dn, string, 0);
  return string;
}

char *
decNumberToEngString (const decNumber * dn, char *string)
{
  decToString (dn, string, 1);
  return string;
}

/* ------------------------------------------------------------------ */
/* to-number -- conversion from numeric string                        */
/*                                                                    */
/* decNumberFromString -- convert string to decNumber                 */
/*   dn        -- the number structure to fill                        */
/*   chars[]   -- the string to convert ('\0' terminated)             */
/*   set       -- the context used for processing any error,          */
/*                determining the maximum precision available         */
/*                (set.digits), determining the maximum and minimum   */
/*                exponent (set.emax and set.emin), determining if    */
/*                extended values are allowed, and checking the       */
/*                rounding mode if overflow occurs or rounding is     */
/*                needed.                                             */
/*                                                                    */
/* The length of the coefficient and the size of the exponent are     */
/* checked by this routine, so the correct error (Underflow or        */
/* Overflow) can be reported or rounding applied, as necessary.       */
/*                                                                    */
/* If bad syntax is detected, the result will be a quiet NaN.         */
/* ------------------------------------------------------------------ */
decNumber *
decNumberFromString (decNumber * dn, const char chars[], decContext * set)
{
  Int exponent = 0;		/* working exponent [assume 0] */
  uByte bits = 0;		/* working flags [assume +ve] */
  Unit *res;			/* where result will be built */
  Unit resbuff[D2U (DECBUFFER + 1)];	/* local buffer in case need temporary */
  Unit *allocres = NULL;	/* -> allocated result, iff allocated */
  Int need;			/* units needed for result */
  Int d = 0;			/* count of digits found in decimal part */
  const char *dotchar = NULL;	/* where dot was found */
  const char *cfirst;		/* -> first character of decimal part */
  const char *last = NULL;	/* -> last digit of decimal part */
  const char *firstexp;		/* -> first significant exponent digit */
  const char *c;		/* work */
  Unit *up;			/* .. */
#if DECDPUN>1
  Int i;			/* .. */
#endif
  Int residue = 0;		/* rounding residue */
  uInt status = 0;		/* error code */

#if DECCHECK
  if (decCheckOperands (DECUNUSED, DECUNUSED, DECUNUSED, set))
    return decNumberZero (dn);
#endif

  do
    {				/* status & malloc protection */
      c = chars;		/* -> input character */
      if (*c == '-')
	{			/* handle leading '-' */
	  bits = DECNEG;
	  c++;
	}
      else if (*c == '+')
	c++;			/* step over leading '+' */
      /* We're at the start of the number [we think] */
      cfirst = c;		/* save */
      for (;; c++)
	{
	  if (*c >= '0' && *c <= '9')
	    {			/* test for Arabic digit */
	      last = c;
	      d++;		/* count of real digits */
	      continue;		/* still in decimal part */
	    }
	  if (*c != '.')
	    break;		/* done with decimal part */
	  /* dot: record, check, and ignore */
	  if (dotchar != NULL)
	    {			/* two dots */
	      last = NULL;	/* indicate bad */
	      break;
	    }			/* .. and go report */
	  dotchar = c;		/* offset into decimal part */
	}			/* c */

      if (last == NULL)
	{			/* no decimal digits, or >1 . */
#if DECSUBSET
	  /* If subset then infinities and NaNs are not allowed */
	  if (!set->extended)
	    {
	      status = DEC_Conversion_syntax;
	      break;		/* all done */
	    }
	  else
	    {
#endif
	      /* Infinities and NaNs are possible, here */
	      decNumberZero (dn);	/* be optimistic */
	      if (decStrEq (c, "Infinity") || decStrEq (c, "Inf"))
		{
		  dn->bits = bits | DECINF;
		  break;	/* all done */
		}
	      else
		{		/* a NaN expected */
		  /* 2003.09.10 NaNs are now permitted to have a sign */
		  status = DEC_Conversion_syntax;	/* assume the worst */
		  dn->bits = bits | DECNAN;	/* assume simple NaN */
		  if (*c == 's' || *c == 'S')
		    {		/* looks like an` sNaN */
		      c++;
		      dn->bits = bits | DECSNAN;
		    }
		  if (*c != 'n' && *c != 'N')
		    break;	/* check caseless "NaN" */
		  c++;
		  if (*c != 'a' && *c != 'A')
		    break;	/* .. */
		  c++;
		  if (*c != 'n' && *c != 'N')
		    break;	/* .. */
		  c++;
		  /* now nothing, or nnnn, expected */
		  /* -> start of integer and skip leading 0s [including plain 0] */
		  for (cfirst = c; *cfirst == '0';)
		    cfirst++;
		  if (*cfirst == '\0')
		    {		/* "NaN" or "sNaN", maybe with all 0s */
		      status = 0;	/* it's good */
		      break;	/* .. */
		    }
		  /* something other than 0s; setup last and d as usual [no dots] */
		  for (c = cfirst;; c++, d++)
		    {
		      if (*c < '0' || *c > '9')
			break;	/* test for Arabic digit */
		      last = c;
		    }
		  if (*c != '\0')
		    break;	/* not all digits */
		  if (d > set->digits)
		    break;	/* too many digits */
		  /* good; drop through and convert the integer */
		  status = 0;
		  bits = dn->bits;	/* for copy-back */
		}		/* NaN expected */
#if DECSUBSET
	    }
#endif
	}			/* last==NULL */

      if (*c != '\0')
	{			/* more there; exponent expected... */
	  Flag nege = 0;	/* 1=negative exponent */
	  if (*c != 'e' && *c != 'E')
	    {
	      status = DEC_Conversion_syntax;
	      break;
	    }

	  /* Found 'e' or 'E' -- now process explicit exponent */
	  /* 1998.07.11: sign no longer required */
	  c++;			/* to (expected) sign */
	  if (*c == '-')
	    {
	      nege = 1;
	      c++;
	    }
	  else if (*c == '+')
	    c++;
	  if (*c == '\0')
	    {
	      status = DEC_Conversion_syntax;
	      break;
	    }

	  for (; *c == '0' && *(c + 1) != '\0';)
	    c++;		/* strip insignificant zeros */
	  firstexp = c;		/* save exponent digit place */
	  for (;; c++)
	    {
	      if (*c < '0' || *c > '9')
		break;		/* not a digit */
	      exponent = X10 (exponent) + (Int) * c - (Int) '0';
	    }			/* c */
	  /* if we didn't end on '\0' must not be a digit */
	  if (*c != '\0')
	    {
	      status = DEC_Conversion_syntax;
	      break;
	    }

	  /* (this next test must be after the syntax check) */
	  /* if it was too long the exponent may have wrapped, so check */
	  /* carefully and set it to a certain overflow if wrap possible */
	  if (c >= firstexp + 9 + 1)
	    {
	      if (c > firstexp + 9 + 1 || *firstexp > '1')
		exponent = DECNUMMAXE * 2;
	      /* [up to 1999999999 is OK, for example 1E-1000000998] */
	    }
	  if (nege)
	    exponent = -exponent;	/* was negative */
	}			/* had exponent */
      /* Here when all inspected; syntax is good */

      /* Handle decimal point... */
      if (dotchar != NULL && dotchar < last)	/* embedded . found, so */
	exponent = exponent - (last - dotchar);	/* .. adjust exponent */
      /* [we can now ignore the .] */

      /* strip leading zeros/dot (leave final if all 0's) */
      for (c = cfirst; c < last; c++)
	{
	  if (*c == '0')
	    d--;		/* 0 stripped */
	  else if (*c != '.')
	    break;
	  cfirst++;		/* step past leader */
	}			/* c */

#if DECSUBSET
      /* We can now make a rapid exit for zeros if !extended */
      if (*cfirst == '0' && !set->extended)
	{
	  decNumberZero (dn);	/* clean result */
	  break;		/* [could be return] */
	}
#endif

      /* OK, the digits string is good.  Copy to the decNumber, or to
         a temporary decNumber if rounding is needed */
      if (d <= set->digits)
	res = dn->lsu;		/* fits into given decNumber */
      else
	{			/* rounding needed */
	  need = D2U (d);	/* units needed */
	  res = resbuff;	/* assume use local buffer */
	  if (need * sizeof (Unit) > sizeof (resbuff))
	    {			/* too big for local */
	      allocres = (Unit *) malloc (need * sizeof (Unit));
	      if (allocres == NULL)
		{
		  status |= DEC_Insufficient_storage;
		  break;
		}
	      res = allocres;
	    }
	}
      /* res now -> number lsu, buffer, or allocated storage for Unit array */

      /* Place the coefficient into the selected Unit array */
#if DECDPUN>1
      i = d % DECDPUN;		/* digits in top unit */
      if (i == 0)
	i = DECDPUN;
      up = res + D2U (d) - 1;	/* -> msu */
      *up = 0;
      for (c = cfirst;; c++)
	{			/* along the digits */
	  if (*c == '.')
	    {			/* ignore . [don't decrement i] */
	      if (c != last)
		continue;
	      break;
	    }
	  *up = (Unit) (X10 (*up) + (Int) * c - (Int) '0');
	  i--;
	  if (i > 0)
	    continue;		/* more for this unit */
	  if (up == res)
	    break;		/* just filled the last unit */
	  i = DECDPUN;
	  up--;
	  *up = 0;
	}			/* c */
#else
      /* DECDPUN==1 */
      up = res;			/* -> lsu */
      for (c = last; c >= cfirst; c--)
	{			/* over each character, from least */
	  if (*c == '.')
	    continue;		/* ignore . [don't step b] */
	  *up = (Unit) ((Int) * c - (Int) '0');
	  up++;
	}			/* c */
#endif

      dn->bits = bits;
      dn->exponent = exponent;
      dn->digits = d;

      /* if not in number (too long) shorten into the number */
      if (d > set->digits)
	decSetCoeff (dn, set, res, d, &residue, &status);

      /* Finally check for overflow or subnormal and round as needed */
      decFinalize (dn, set, &residue, &status);
      /* decNumberShow(dn); */
    }
  while (0);			/* [for break] */

  if (allocres != NULL)
    free (allocres);		/* drop any storage we used */
  if (status != 0)
    decStatus (dn, status, set);
  return dn;
}

/* ================================================================== */
/* Operators                                                          */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* decNumberAbs -- absolute value operator                            */
/*                                                                    */
/*   This computes C = abs(A)                                         */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* This has the same effect as decNumberPlus unless A is negative,    */
/* in which case it has the same effect as decNumberMinus.            */
/* ------------------------------------------------------------------ */
decNumber *
decNumberAbs (decNumber * res, const decNumber * rhs, decContext * set)
{
  decNumber dzero;		/* for 0 */
  uInt status = 0;		/* accumulator */

#if DECCHECK
  if (decCheckOperands (res, DECUNUSED, rhs, set))
    return res;
#endif

  decNumberZero (&dzero);	/* set 0 */
  dzero.exponent = rhs->exponent;	/* [no coefficient expansion] */
  decAddOp (res, &dzero, rhs, set, (uByte) (rhs->bits & DECNEG), &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberAdd -- add two Numbers                                    */
/*                                                                    */
/*   This computes C = A + B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X+X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* This just calls the routine shared with Subtract                   */
decNumber *
decNumberAdd (decNumber * res, const decNumber * lhs,
	      const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decAddOp (res, lhs, rhs, set, 0, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberCompare -- compare two Numbers                            */
/*                                                                    */
/*   This computes C = A ? B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for one digit.                                   */
/* ------------------------------------------------------------------ */
decNumber *
decNumberCompare (decNumber * res, const decNumber * lhs,
		  const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decCompareOp (res, lhs, rhs, set, COMPARE, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberDivide -- divide one number by another                    */
/*                                                                    */
/*   This computes C = A / B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X/X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberDivide (decNumber * res, const decNumber * lhs,
		 const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decDivideOp (res, lhs, rhs, set, DIVIDE, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberDivideInteger -- divide and return integer quotient       */
/*                                                                    */
/*   This computes C = A # B, where # is the integer divide operator  */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X#X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberDivideInteger (decNumber * res, const decNumber * lhs,
			const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decDivideOp (res, lhs, rhs, set, DIVIDEINT, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberMax -- compare two Numbers and return the maximum         */
/*                                                                    */
/*   This computes C = A ? B, returning the maximum or A if equal     */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberMax (decNumber * res, const decNumber * lhs,
	      const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decCompareOp (res, lhs, rhs, set, COMPMAX, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberMin -- compare two Numbers and return the minimum         */
/*                                                                    */
/*   This computes C = A ? B, returning the minimum or A if equal     */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberMin (decNumber * res, const decNumber * lhs,
	      const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decCompareOp (res, lhs, rhs, set, COMPMIN, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberMinus -- prefix minus operator                            */
/*                                                                    */
/*   This computes C = 0 - A                                          */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* We simply use AddOp for the subtract, which will do the necessary. */
/* ------------------------------------------------------------------ */
decNumber *
decNumberMinus (decNumber * res, const decNumber * rhs, decContext * set)
{
  decNumber dzero;
  uInt status = 0;		/* accumulator */

#if DECCHECK
  if (decCheckOperands (res, DECUNUSED, rhs, set))
    return res;
#endif

  decNumberZero (&dzero);	/* make 0 */
  dzero.exponent = rhs->exponent;	/* [no coefficient expansion] */
  decAddOp (res, &dzero, rhs, set, DECNEG, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberPlus -- prefix plus operator                              */
/*                                                                    */
/*   This computes C = 0 + A                                          */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* We simply use AddOp; Add will take fast path after preparing A.    */
/* Performance is a concern here, as this routine is often used to    */
/* check operands and apply rounding and overflow/underflow testing.  */
/* ------------------------------------------------------------------ */
decNumber *
decNumberPlus (decNumber * res, const decNumber * rhs, decContext * set)
{
  decNumber dzero;
  uInt status = 0;		/* accumulator */

#if DECCHECK
  if (decCheckOperands (res, DECUNUSED, rhs, set))
    return res;
#endif

  decNumberZero (&dzero);	/* make 0 */
  dzero.exponent = rhs->exponent;	/* [no coefficient expansion] */
  decAddOp (res, &dzero, rhs, set, 0, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberMultiply -- multiply two Numbers                          */
/*                                                                    */
/*   This computes C = A x B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X+X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberMultiply (decNumber * res, const decNumber * lhs,
		   const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decMultiplyOp (res, lhs, rhs, set, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberNormalize -- remove trailing zeros                        */
/*                                                                    */
/*   This computes C = 0 + A, and normalizes the result               */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberNormalize (decNumber * res, const decNumber * rhs, decContext * set)
{
  decNumber *allocrhs = NULL;	/* non-NULL if rounded rhs allocated */
  uInt status = 0;		/* as usual */
  Int residue = 0;		/* as usual */
  Int dropped;			/* work */

#if DECCHECK
  if (decCheckOperands (res, DECUNUSED, rhs, set))
    return res;
#endif

  do
    {				/* protect allocated storage */
#if DECSUBSET
      if (!set->extended)
	{
	  /* reduce operand and set lostDigits status, as needed */
	  if (rhs->digits > set->digits)
	    {
	      allocrhs = decRoundOperand (rhs, set, &status);
	      if (allocrhs == NULL)
		break;
	      rhs = allocrhs;
	    }
	}
#endif
      /* [following code does not require input rounding] */

      /* specials copy through, except NaNs need care */
      if (decNumberIsNaN (rhs))
	{
	  decNaNs (res, rhs, NULL, &status);
	  break;
	}

      /* reduce result to the requested length and copy to result */
      decCopyFit (res, rhs, set, &residue, &status);	/* copy & round */
      decFinish (res, set, &residue, &status);	/* cleanup/set flags */
      decTrim (res, 1, &dropped);	/* normalize in place */
    }
  while (0);			/* end protected */

  if (allocrhs != NULL)
    free (allocrhs);		/* .. */
  if (status != 0)
    decStatus (res, status, set);	/* then report status */
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberPower -- raise a number to an integer power               */
/*                                                                    */
/*   This computes C = A ** B                                         */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X**X)        */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Specification restriction: abs(n) must be <=999999999              */
/* ------------------------------------------------------------------ */
decNumber *
decNumberPower (decNumber * res, const decNumber * lhs,
		const decNumber * rhs, decContext * set)
{
  decNumber *alloclhs = NULL;	/* non-NULL if rounded lhs allocated */
  decNumber *allocrhs = NULL;	/* .., rhs */
  decNumber *allocdac = NULL;	/* -> allocated acc buffer, iff used */
  const decNumber *inrhs = rhs;	/* save original rhs */
  Int reqdigits = set->digits;	/* requested DIGITS */
  Int n;			/* RHS in binary */
  Int i;			/* work */
#if DECSUBSET
  Int dropped;			/* .. */
#endif
  uInt needbytes;		/* buffer size needed */
  Flag seenbit;			/* seen a bit while powering */
  Int residue = 0;		/* rounding residue */
  uInt status = 0;		/* accumulator */
  uByte bits = 0;		/* result sign if errors */
  decContext workset;		/* working context */
  decNumber dnOne;		/* work value 1... */
  /* local accumulator buffer [a decNumber, with digits+elength+1 digits] */
  uByte dacbuff[sizeof (decNumber) + D2U (DECBUFFER + 9) * sizeof (Unit)];
  /* same again for possible 1/lhs calculation */
  uByte lhsbuff[sizeof (decNumber) + D2U (DECBUFFER + 9) * sizeof (Unit)];
  decNumber *dac = (decNumber *) dacbuff;	/* -> result accumulator */

#if DECCHECK
  if (decCheckOperands (res, lhs, rhs, set))
    return res;
#endif

  do
    {				/* protect allocated storage */
#if DECSUBSET
      if (!set->extended)
	{
	  /* reduce operands and set lostDigits status, as needed */
	  if (lhs->digits > reqdigits)
	    {
	      alloclhs = decRoundOperand (lhs, set, &status);
	      if (alloclhs == NULL)
		break;
	      lhs = alloclhs;
	    }
	  /* rounding won't affect the result, but we might signal lostDigits */
	  /* as well as the error for non-integer [x**y would need this too] */
	  if (rhs->digits > reqdigits)
	    {
	      allocrhs = decRoundOperand (rhs, set, &status);
	      if (allocrhs == NULL)
		break;
	      rhs = allocrhs;
	    }
	}
#endif
      /* [following code does not require input rounding] */

      /* handle rhs Infinity */
      if (decNumberIsInfinite (rhs))
	{
	  status |= DEC_Invalid_operation;	/* bad */
	  break;
	}
      /* handle NaNs */
      if ((lhs->bits | rhs->bits) & (DECNAN | DECSNAN))
	{
	  decNaNs (res, lhs, rhs, &status);
	  break;
	}

      /* Original rhs must be an integer that fits and is in range */
#if DECSUBSET
      n = decGetInt (inrhs, set);
#else
      n = decGetInt (inrhs);
#endif
      if (n == BADINT || n > 999999999 || n < -999999999)
	{
	  status |= DEC_Invalid_operation;
	  break;
	}
      if (n < 0)
	{			/* negative */
	  n = -n;		/* use the absolute value */
	}
      if (decNumberIsNegative (lhs)	/* -x .. */
	  && (n & 0x00000001))
	bits = DECNEG;		/* .. to an odd power */

      /* handle LHS infinity */
      if (decNumberIsInfinite (lhs))
	{			/* [NaNs already handled] */
	  uByte rbits = rhs->bits;	/* save */
	  decNumberZero (res);
	  if (n == 0)
	    *res->lsu = 1;	/* [-]Inf**0 => 1 */
	  else
	    {
	      if (!(rbits & DECNEG))
		bits |= DECINF;	/* was not a **-n */
	      /* [otherwise will be 0 or -0] */
	      res->bits = bits;
	    }
	  break;
	}

      /* clone the context */
      workset = *set;		/* copy all fields */
      /* calculate the working DIGITS */
      workset.digits = reqdigits + (inrhs->digits + inrhs->exponent) + 1;
      /* it's an error if this is more than we can handle */
      if (workset.digits > DECNUMMAXP)
	{
	  status |= DEC_Invalid_operation;
	  break;
	}

      /* workset.digits is the count of digits for the accumulator we need */
      /* if accumulator is too long for local storage, then allocate */
      needbytes =
	sizeof (decNumber) + (D2U (workset.digits) - 1) * sizeof (Unit);
      /* [needbytes also used below if 1/lhs needed] */
      if (needbytes > sizeof (dacbuff))
	{
	  allocdac = (decNumber *) malloc (needbytes);
	  if (allocdac == NULL)
	    {			/* hopeless -- abandon */
	      status |= DEC_Insufficient_storage;
	      break;
	    }
	  dac = allocdac;	/* use the allocated space */
	}
      decNumberZero (dac);	/* acc=1 */
      *dac->lsu = 1;		/* .. */

      if (n == 0)
	{			/* x**0 is usually 1 */
	  /* 0**0 is bad unless subset, when it becomes 1 */
	  if (ISZERO (lhs)
#if DECSUBSET
	      && set->extended
#endif
	    )
	    status |= DEC_Invalid_operation;
	  else
	    decNumberCopy (res, dac);	/* copy the 1 */
	  break;
	}

      /* if a negative power we'll need the constant 1, and if not subset */
      /* we'll invert the lhs now rather than inverting the result later */
      if (decNumberIsNegative (rhs))
	{			/* was a **-n [hence digits>0] */
	  decNumber * newlhs;
	  decNumberCopy (&dnOne, dac);	/* dnOne=1;  [needed now or later] */
#if DECSUBSET
	  if (set->extended)
	    {			/* need to calculate 1/lhs */
#endif
	      /* divide lhs into 1, putting result in dac [dac=1/dac] */
	      decDivideOp (dac, &dnOne, lhs, &workset, DIVIDE, &status);
	      if (alloclhs != NULL)
		{
		  free (alloclhs);	/* done with intermediate */
		  alloclhs = NULL;	/* indicate freed */
		}
	      /* now locate or allocate space for the inverted lhs */
	      if (needbytes > sizeof (lhsbuff))
		{
		  alloclhs = (decNumber *) malloc (needbytes);
		  if (alloclhs == NULL)
		    {		/* hopeless -- abandon */
		      status |= DEC_Insufficient_storage;
		      break;
		    }
		  newlhs = alloclhs;	/* use the allocated space */
		}
	      else
		newlhs = (decNumber *) lhsbuff;	/* use stack storage */
	      /* [lhs now points to buffer or allocated storage] */
	      decNumberCopy (newlhs, dac);	/* copy the 1/lhs */
	      decNumberCopy (dac, &dnOne);	/* restore acc=1 */
	      lhs = newlhs;
#if DECSUBSET
	    }
#endif
	}

      /* Raise-to-the-power loop... */
      seenbit = 0;		/* set once we've seen a 1-bit */
      for (i = 1;; i++)
	{			/* for each bit [top bit ignored] */
	  /* abandon if we have had overflow or terminal underflow */
	  if (status & (DEC_Overflow | DEC_Underflow))
	    {			/* interesting? */
	      if (status & DEC_Overflow || ISZERO (dac))
		break;
	    }
	  /* [the following two lines revealed an optimizer bug in a C++ */
	  /* compiler, with symptom: 5**3 -> 25, when n=n+n was used] */
	  n = n << 1;		/* move next bit to testable position */
	  if (n < 0)
	    {			/* top bit is set */
	      seenbit = 1;	/* OK, we're off */
	      decMultiplyOp (dac, dac, lhs, &workset, &status);	/* dac=dac*x */
	    }
	  if (i == 31)
	    break;		/* that was the last bit */
	  if (!seenbit)
	    continue;		/* we don't have to square 1 */
	  decMultiplyOp (dac, dac, dac, &workset, &status);	/* dac=dac*dac [square] */
	}			/*i *//* 32 bits */

      /* complete internal overflow or underflow processing */
      if (status & (DEC_Overflow | DEC_Subnormal))
	{
#if DECSUBSET
	  /* If subset, and power was negative, reverse the kind of -erflow */
	  /* [1/x not yet done] */
	  if (!set->extended && decNumberIsNegative (rhs))
	    {
	      if (status & DEC_Overflow)
		status ^= DEC_Overflow | DEC_Underflow | DEC_Subnormal;
	      else
		{		/* trickier -- Underflow may or may not be set */
		  status &= ~(DEC_Underflow | DEC_Subnormal);	/* [one or both] */
		  status |= DEC_Overflow;
		}
	    }
#endif
	  dac->bits = (dac->bits & ~DECNEG) | bits;	/* force correct sign */
	  /* round subnormals [to set.digits rather than workset.digits] */
	  /* or set overflow result similarly as required */
	  decFinalize (dac, set, &residue, &status);
	  decNumberCopy (res, dac);	/* copy to result (is now OK length) */
	  break;
	}

#if DECSUBSET
      if (!set->extended &&	/* subset math */
	  decNumberIsNegative (rhs))
	{			/* was a **-n [hence digits>0] */
	  /* so divide result into 1 [dac=1/dac] */
	  decDivideOp (dac, &dnOne, dac, &workset, DIVIDE, &status);
	}
#endif

      /* reduce result to the requested length and copy to result */
      decCopyFit (res, dac, set, &residue, &status);
      decFinish (res, set, &residue, &status);	/* final cleanup */
#if DECSUBSET
      if (!set->extended)
	decTrim (res, 0, &dropped);	/* trailing zeros */
#endif
    }
  while (0);			/* end protected */

  if (allocdac != NULL)
    free (allocdac);		/* drop any storage we used */
  if (allocrhs != NULL)
    free (allocrhs);		/* .. */
  if (alloclhs != NULL)
    free (alloclhs);		/* .. */
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberQuantize -- force exponent to requested value             */
/*                                                                    */
/*   This computes C = op(A, B), where op adjusts the coefficient     */
/*   of C (by rounding or shifting) such that the exponent (-scale)   */
/*   of C has exponent of B.  The numerical value of C will equal A,  */
/*   except for the effects of any rounding that occurred.            */
/*                                                                    */
/*   res is C, the result.  C may be A or B                           */
/*   lhs is A, the number to adjust                                   */
/*   rhs is B, the number with exponent to match                      */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Unless there is an error or the result is infinite, the exponent   */
/* after the operation is guaranteed to be equal to that of B.        */
/* ------------------------------------------------------------------ */
decNumber *
decNumberQuantize (decNumber * res, const decNumber * lhs,
		   const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decQuantizeOp (res, lhs, rhs, set, 1, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberRescale -- force exponent to requested value              */
/*                                                                    */
/*   This computes C = op(A, B), where op adjusts the coefficient     */
/*   of C (by rounding or shifting) such that the exponent (-scale)   */
/*   of C has the value B.  The numerical value of C will equal A,    */
/*   except for the effects of any rounding that occurred.            */
/*                                                                    */
/*   res is C, the result.  C may be A or B                           */
/*   lhs is A, the number to adjust                                   */
/*   rhs is B, the requested exponent                                 */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Unless there is an error or the result is infinite, the exponent   */
/* after the operation is guaranteed to be equal to B.                */
/* ------------------------------------------------------------------ */
decNumber *
decNumberRescale (decNumber * res, const decNumber * lhs,
		  const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decQuantizeOp (res, lhs, rhs, set, 0, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberRemainder -- divide and return remainder                  */
/*                                                                    */
/*   This computes C = A % B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X%X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberRemainder (decNumber * res, const decNumber * lhs,
		    const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decDivideOp (res, lhs, rhs, set, REMAINDER, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberRemainderNear -- divide and return remainder from nearest */
/*                                                                    */
/*   This computes C = A % B, where % is the IEEE remainder operator  */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X%X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberRemainderNear (decNumber * res, const decNumber * lhs,
			const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */
  decDivideOp (res, lhs, rhs, set, REMNEAR, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberSameQuantum -- test for equal exponents                   */
/*                                                                    */
/*   res is the result number, which will contain either 0 or 1       */
/*   lhs is a number to test                                          */
/*   rhs is the second (usually a pattern)                            */
/*                                                                    */
/* No errors are possible and no context is needed.                   */
/* ------------------------------------------------------------------ */
decNumber *
decNumberSameQuantum (decNumber * res, const decNumber * lhs, const decNumber * rhs)
{
  uByte merged;			/* merged flags */
  Unit ret = 0;			/* return value */

#if DECCHECK
  if (decCheckOperands (res, lhs, rhs, DECUNUSED))
    return res;
#endif

  merged = (lhs->bits | rhs->bits) & DECSPECIAL;
  if (merged)
    {
      if (decNumberIsNaN (lhs) && decNumberIsNaN (rhs))
	ret = 1;
      else if (decNumberIsInfinite (lhs) && decNumberIsInfinite (rhs))
	ret = 1;
      /* [anything else with a special gives 0] */
    }
  else if (lhs->exponent == rhs->exponent)
    ret = 1;

  decNumberZero (res);		/* OK to overwrite an operand */
  *res->lsu = ret;
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberSquareRoot -- square root operator                        */
/*                                                                    */
/*   This computes C = squareroot(A)                                  */
/*                                                                    */
/*   res is C, the result.  C may be A                                */
/*   rhs is A                                                         */
/*   set is the context; note that rounding mode has no effect        */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* This uses the following varying-precision algorithm in:            */
/*                                                                    */
/*   Properly Rounded Variable Precision Square Root, T. E. Hull and  */
/*   A. Abrham, ACM Transactions on Mathematical Software, Vol 11 #3, */
/*   pp229-237, ACM, September 1985.                                  */
/*                                                                    */
/* % [Reformatted original Numerical Turing source code follows.]     */
/* function sqrt(x : real) : real                                     */
/* % sqrt(x) returns the properly rounded approximation to the square */
/* % root of x, in the precision of the calling environment, or it    */
/* % fails if x < 0.                                                  */
/* % t e hull and a abrham, august, 1984                              */
/* if x <= 0 then                                                     */
/*   if x < 0 then                                                    */
/*     assert false                                                   */
/*   else                                                             */
/*     result 0                                                       */
/*   end if                                                           */
/* end if                                                             */
/* var f := setexp(x, 0)  % fraction part of x   [0.1 <= x < 1]       */
/* var e := getexp(x)     % exponent part of x                        */
/* var approx : real                                                  */
/* if e mod 2 = 0  then                                               */
/*   approx := .259 + .819 * f   % approx to root of f                */
/* else                                                               */
/*   f := f/l0                   % adjustments                        */
/*   e := e + 1                  %   for odd                          */
/*   approx := .0819 + 2.59 * f  %   exponent                         */
/* end if                                                             */
/*                                                                    */
/* var p:= 3                                                          */
/* const maxp := currentprecision + 2                                 */
/* loop                                                               */
/*   p := min(2*p - 2, maxp)     % p = 4,6,10, . . . , maxp           */
/*   precision p                                                      */
/*   approx := .5 * (approx + f/approx)                               */
/*   exit when p = maxp                                               */
/* end loop                                                           */
/*                                                                    */
/* % approx is now within 1 ulp of the properly rounded square root   */
/* % of f; to ensure proper rounding, compare squares of (approx -    */
/* % l/2 ulp) and (approx + l/2 ulp) with f.                          */
/* p := currentprecision                                              */
/* begin                                                              */
/*   precision p + 2                                                  */
/*   const approxsubhalf := approx - setexp(.5, -p)                   */
/*   if mulru(approxsubhalf, approxsubhalf) > f then                  */
/*     approx := approx - setexp(.l, -p + 1)                          */
/*   else                                                             */
/*     const approxaddhalf := approx + setexp(.5, -p)                 */
/*     if mulrd(approxaddhalf, approxaddhalf) < f then                */
/*       approx := approx + setexp(.l, -p + 1)                        */
/*     end if                                                         */
/*   end if                                                           */
/* end                                                                */
/* result setexp(approx, e div 2)  % fix exponent                     */
/* end sqrt                                                           */
/* ------------------------------------------------------------------ */
decNumber *
decNumberSquareRoot (decNumber * res, const decNumber * rhs, decContext * set)
{
  decContext workset, approxset;	/* work contexts */
  decNumber dzero;		/* used for constant zero */
  Int maxp = set->digits + 2;	/* largest working precision */
  Int residue = 0;		/* rounding residue */
  uInt status = 0, ignore = 0;	/* status accumulators */
  Int exp;			/* working exponent */
  Int ideal;			/* ideal (preferred) exponent */
  uInt needbytes;		/* work */
  Int dropped;			/* .. */

  decNumber *allocrhs = NULL;	/* non-NULL if rounded rhs allocated */
  /* buffer for f [needs +1 in case DECBUFFER 0] */
  uByte buff[sizeof (decNumber) + (D2U (DECBUFFER + 1) - 1) * sizeof (Unit)];
  /* buffer for a [needs +2 to match maxp] */
  uByte bufa[sizeof (decNumber) + (D2U (DECBUFFER + 2) - 1) * sizeof (Unit)];
  /* buffer for temporary, b [must be same size as a] */
  uByte bufb[sizeof (decNumber) + (D2U (DECBUFFER + 2) - 1) * sizeof (Unit)];
  decNumber *allocbuff = NULL;	/* -> allocated buff, iff allocated */
  decNumber *allocbufa = NULL;	/* -> allocated bufa, iff allocated */
  decNumber *allocbufb = NULL;	/* -> allocated bufb, iff allocated */
  decNumber *f = (decNumber *) buff;	/* reduced fraction */
  decNumber *a = (decNumber *) bufa;	/* approximation to result */
  decNumber *b = (decNumber *) bufb;	/* intermediate result */
  /* buffer for temporary variable, up to 3 digits */
  uByte buft[sizeof (decNumber) + (D2U (3) - 1) * sizeof (Unit)];
  decNumber *t = (decNumber *) buft;	/* up-to-3-digit constant or work */

#if DECCHECK
  if (decCheckOperands (res, DECUNUSED, rhs, set))
    return res;
#endif

  do
    {				/* protect allocated storage */
#if DECSUBSET
      if (!set->extended)
	{
	  /* reduce operand and set lostDigits status, as needed */
	  if (rhs->digits > set->digits)
	    {
	      allocrhs = decRoundOperand (rhs, set, &status);
	      if (allocrhs == NULL)
		break;
	      /* [Note: 'f' allocation below could reuse this buffer if */
	      /* used, but as this is rare we keep them separate for clarity.] */
	      rhs = allocrhs;
	    }
	}
#endif
      /* [following code does not require input rounding] */

      /* handle infinities and NaNs */
      if (rhs->bits & DECSPECIAL)
	{
	  if (decNumberIsInfinite (rhs))
	    {			/* an infinity */
	      if (decNumberIsNegative (rhs))
		status |= DEC_Invalid_operation;
	      else
		decNumberCopy (res, rhs);	/* +Infinity */
	    }
	  else
	    decNaNs (res, rhs, NULL, &status);	/* a NaN */
	  break;
	}

      /* calculate the ideal (preferred) exponent [floor(exp/2)] */
      /* [We would like to write: ideal=rhs->exponent>>1, but this */
      /* generates a compiler warning.  Generated code is the same.] */
      ideal = (rhs->exponent & ~1) / 2;	/* target */

      /* handle zeros */
      if (ISZERO (rhs))
	{
	  decNumberCopy (res, rhs);	/* could be 0 or -0 */
	  res->exponent = ideal;	/* use the ideal [safe] */
	  break;
	}

      /* any other -x is an oops */
      if (decNumberIsNegative (rhs))
	{
	  status |= DEC_Invalid_operation;
	  break;
	}

      /* we need space for three working variables */
      /*   f -- the same precision as the RHS, reduced to 0.01->0.99... */
      /*   a -- Hull's approx -- precision, when assigned, is */
      /*        currentprecision (we allow +2 for use as temporary) */
      /*   b -- intermediate temporary result */
      /* if any is too long for local storage, then allocate */
      needbytes =
	sizeof (decNumber) + (D2U (rhs->digits) - 1) * sizeof (Unit);
      if (needbytes > sizeof (buff))
	{
	  allocbuff = (decNumber *) malloc (needbytes);
	  if (allocbuff == NULL)
	    {			/* hopeless -- abandon */
	      status |= DEC_Insufficient_storage;
	      break;
	    }
	  f = allocbuff;	/* use the allocated space */
	}
      /* a and b both need to be able to hold a maxp-length number */
      needbytes = sizeof (decNumber) + (D2U (maxp) - 1) * sizeof (Unit);
      if (needbytes > sizeof (bufa))
	{			/* [same applies to b] */
	  allocbufa = (decNumber *) malloc (needbytes);
	  allocbufb = (decNumber *) malloc (needbytes);
	  if (allocbufa == NULL || allocbufb == NULL)
	    {			/* hopeless */
	      status |= DEC_Insufficient_storage;
	      break;
	    }
	  a = allocbufa;	/* use the allocated space */
	  b = allocbufb;	/* .. */
	}

      /* copy rhs -> f, save exponent, and reduce so 0.1 <= f < 1 */
      decNumberCopy (f, rhs);
      exp = f->exponent + f->digits;	/* adjusted to Hull rules */
      f->exponent = -(f->digits);	/* to range */

      /* set up working contexts (the second is used for Numerical */
      /* Turing assignment) */
      decContextDefault (&workset, DEC_INIT_DECIMAL64);
      decContextDefault (&approxset, DEC_INIT_DECIMAL64);
      approxset.digits = set->digits;	/* approx's length */

      /* [Until further notice, no error is possible and status bits */
      /* (Rounded, etc.) should be ignored, not accumulated.] */

      /* Calculate initial approximation, and allow for odd exponent */
      workset.digits = set->digits;	/* p for initial calculation */
      t->bits = 0;
      t->digits = 3;
      a->bits = 0;
      a->digits = 3;
      if ((exp & 1) == 0)
	{			/* even exponent */
	  /* Set t=0.259, a=0.819 */
	  t->exponent = -3;
	  a->exponent = -3;
#if DECDPUN>=3
	  t->lsu[0] = 259;
	  a->lsu[0] = 819;
#elif DECDPUN==2
	  t->lsu[0] = 59;
	  t->lsu[1] = 2;
	  a->lsu[0] = 19;
	  a->lsu[1] = 8;
#else
	  t->lsu[0] = 9;
	  t->lsu[1] = 5;
	  t->lsu[2] = 2;
	  a->lsu[0] = 9;
	  a->lsu[1] = 1;
	  a->lsu[2] = 8;
#endif
	}
      else
	{			/* odd exponent */
	  /* Set t=0.0819, a=2.59 */
	  f->exponent--;	/* f=f/10 */
	  exp++;		/* e=e+1 */
	  t->exponent = -4;
	  a->exponent = -2;
#if DECDPUN>=3
	  t->lsu[0] = 819;
	  a->lsu[0] = 259;
#elif DECDPUN==2
	  t->lsu[0] = 19;
	  t->lsu[1] = 8;
	  a->lsu[0] = 59;
	  a->lsu[1] = 2;
#else
	  t->lsu[0] = 9;
	  t->lsu[1] = 1;
	  t->lsu[2] = 8;
	  a->lsu[0] = 9;
	  a->lsu[1] = 5;
	  a->lsu[2] = 2;
#endif
	}
      decMultiplyOp (a, a, f, &workset, &ignore);	/* a=a*f */
      decAddOp (a, a, t, &workset, 0, &ignore);	/* ..+t */
      /* [a is now the initial approximation for sqrt(f), calculated with */
      /* currentprecision, which is also a's precision.] */

      /* the main calculation loop */
      decNumberZero (&dzero);	/* make 0 */
      decNumberZero (t);	/* set t = 0.5 */
      t->lsu[0] = 5;		/* .. */
      t->exponent = -1;		/* .. */
      workset.digits = 3;	/* initial p */
      for (;;)
	{
	  /* set p to min(2*p - 2, maxp)  [hence 3; or: 4, 6, 10, ... , maxp] */
	  workset.digits = workset.digits * 2 - 2;
	  if (workset.digits > maxp)
	    workset.digits = maxp;
	  /* a = 0.5 * (a + f/a) */
	  /* [calculated at p then rounded to currentprecision] */
	  decDivideOp (b, f, a, &workset, DIVIDE, &ignore);	/* b=f/a */
	  decAddOp (b, b, a, &workset, 0, &ignore);	/* b=b+a */
	  decMultiplyOp (a, b, t, &workset, &ignore);	/* a=b*0.5 */
	  /* assign to approx [round to length] */
	  decAddOp (a, &dzero, a, &approxset, 0, &ignore);
	  if (workset.digits == maxp)
	    break;		/* just did final */
	}			/* loop */

      /* a is now at currentprecision and within 1 ulp of the properly */
      /* rounded square root of f; to ensure proper rounding, compare */
      /* squares of (a - l/2 ulp) and (a + l/2 ulp) with f. */
      /* Here workset.digits=maxp and t=0.5 */
      workset.digits--;		/* maxp-1 is OK now */
      t->exponent = -set->digits - 1;	/* make 0.5 ulp */
      decNumberCopy (b, a);
      decAddOp (b, b, t, &workset, DECNEG, &ignore);	/* b = a - 0.5 ulp */
      workset.round = DEC_ROUND_UP;
      decMultiplyOp (b, b, b, &workset, &ignore);	/* b = mulru(b, b) */
      decCompareOp (b, f, b, &workset, COMPARE, &ignore);	/* b ? f, reversed */
      if (decNumberIsNegative (b))
	{			/* f < b [i.e., b > f] */
	  /* this is the more common adjustment, though both are rare */
	  t->exponent++;	/* make 1.0 ulp */
	  t->lsu[0] = 1;	/* .. */
	  decAddOp (a, a, t, &workset, DECNEG, &ignore);	/* a = a - 1 ulp */
	  /* assign to approx [round to length] */
	  decAddOp (a, &dzero, a, &approxset, 0, &ignore);
	}
      else
	{
	  decNumberCopy (b, a);
	  decAddOp (b, b, t, &workset, 0, &ignore);	/* b = a + 0.5 ulp */
	  workset.round = DEC_ROUND_DOWN;
	  decMultiplyOp (b, b, b, &workset, &ignore);	/* b = mulrd(b, b) */
	  decCompareOp (b, b, f, &workset, COMPARE, &ignore);	/* b ? f */
	  if (decNumberIsNegative (b))
	    {			/* b < f */
	      t->exponent++;	/* make 1.0 ulp */
	      t->lsu[0] = 1;	/* .. */
	      decAddOp (a, a, t, &workset, 0, &ignore);	/* a = a + 1 ulp */
	      /* assign to approx [round to length] */
	      decAddOp (a, &dzero, a, &approxset, 0, &ignore);
	    }
	}
      /* [no errors are possible in the above, and rounding/inexact during */
      /* estimation are irrelevant, so status was not accumulated] */

      /* Here, 0.1 <= a < 1  [Hull] */
      a->exponent += exp / 2;	/* set correct exponent */

      /* Process Subnormals */
      decFinalize (a, set, &residue, &status);

      /* count dropable zeros [after any subnormal rounding] */
      decNumberCopy (b, a);
      decTrim (b, 1, &dropped);	/* [drops trailing zeros] */

      /* Finally set Inexact and Rounded.  The answer can only be exact if */
      /* it is short enough so that squaring it could fit in set->digits, */
      /* so this is the only (relatively rare) time we have to check */
      /* carefully */
      if (b->digits * 2 - 1 > set->digits)
	{			/* cannot fit */
	  status |= DEC_Inexact | DEC_Rounded;
	}
      else
	{			/* could be exact/unrounded */
	  uInt mstatus = 0;	/* local status */
	  decMultiplyOp (b, b, b, &workset, &mstatus);	/* try the multiply */
	  if (mstatus != 0)
	    {			/* result won't fit */
	      status |= DEC_Inexact | DEC_Rounded;
	    }
	  else
	    {			/* plausible */
	      decCompareOp (t, b, rhs, &workset, COMPARE, &mstatus);	/* b ? rhs */
	      if (!ISZERO (t))
		{
		  status |= DEC_Inexact | DEC_Rounded;
		}
	      else
		{		/* is Exact */
		  /* here, dropped is the count of trailing zeros in 'a' */
		  /* use closest exponent to ideal... */
		  Int todrop = ideal - a->exponent;	/* most we can drop */

		  if (todrop < 0)
		    {		/* ideally would add 0s */
		      status |= DEC_Rounded;
		    }
		  else
		    {		/* unrounded */
		      if (dropped < todrop)
			todrop = dropped;	/* clamp to those available */
		      if (todrop > 0)
			{	/* OK, some to drop */
			  decShiftToLeast (a->lsu, D2U (a->digits), todrop);
			  a->exponent += todrop;	/* maintain numerical value */
			  a->digits -= todrop;	/* new length */
			}
		    }
		}
	    }
	}
      decNumberCopy (res, a);	/* assume this is the result */
    }
  while (0);			/* end protected */

  if (allocbuff != NULL)
    free (allocbuff);		/* drop any storage we used */
  if (allocbufa != NULL)
    free (allocbufa);		/* .. */
  if (allocbufb != NULL)
    free (allocbufb);		/* .. */
  if (allocrhs != NULL)
    free (allocrhs);		/* .. */
  if (status != 0)
    decStatus (res, status, set);	/* then report status */
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberSubtract -- subtract two Numbers                          */
/*                                                                    */
/*   This computes C = A - B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X-X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberSubtract (decNumber * res, const decNumber * lhs,
		   const decNumber * rhs, decContext * set)
{
  uInt status = 0;		/* accumulator */

  decAddOp (res, lhs, rhs, set, DECNEG, &status);
  if (status != 0)
    decStatus (res, status, set);
  return res;
}

/* ------------------------------------------------------------------ */
/* decNumberToIntegralValue -- round-to-integral-value                */
/*                                                                    */
/*   res is the result                                                */
/*   rhs is input number                                              */
/*   set is the context                                               */
/*                                                                    */
/* res must have space for any value of rhs.                          */
/*                                                                    */
/* This implements the IEEE special operator and therefore treats     */
/* special values as valid, and also never sets Inexact.  For finite  */
/* numbers it returns rescale(rhs, 0) if rhs->exponent is <0.         */
/* Otherwise the result is rhs (so no error is possible).             */
/*                                                                    */
/* The context is used for rounding mode and status after sNaN, but   */
/* the digits setting is ignored.                                     */
/* ------------------------------------------------------------------ */
decNumber *
decNumberToIntegralValue (decNumber * res, const decNumber * rhs, decContext * set)
{
  decNumber dn;
  decContext workset;		/* working context */

#if DECCHECK
  if (decCheckOperands (res, DECUNUSED, rhs, set))
    return res;
#endif

  /* handle infinities and NaNs */
  if (rhs->bits & DECSPECIAL)
    {
      uInt status = 0;
      if (decNumberIsInfinite (rhs))
	decNumberCopy (res, rhs);	/* an Infinity */
      else
	decNaNs (res, rhs, NULL, &status);	/* a NaN */
      if (status != 0)
	decStatus (res, status, set);
      return res;
    }

  /* we have a finite number; no error possible */
  if (rhs->exponent >= 0)
    return decNumberCopy (res, rhs);
  /* that was easy, but if negative exponent we have work to do... */
  workset = *set;		/* clone rounding, etc. */
  workset.digits = rhs->digits;	/* no length rounding */
  workset.traps = 0;		/* no traps */
  decNumberZero (&dn);		/* make a number with exponent 0 */
  return decNumberQuantize (res, rhs, &dn, &workset);
}

/* ================================================================== */
/* Utility routines                                                   */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* decNumberCopy -- copy a number                                     */
/*                                                                    */
/*   dest is the target decNumber                                     */
/*   src  is the source decNumber                                     */
/*   returns dest                                                     */
/*                                                                    */
/* (dest==src is allowed and is a no-op)                              */
/* All fields are updated as required.  This is a utility operation,  */
/* so special values are unchanged and no error is possible.          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberCopy (decNumber * dest, const decNumber * src)
{

#if DECCHECK
  if (src == NULL)
    return decNumberZero (dest);
#endif

  if (dest == src)
    return dest;		/* no copy required */

  /* We use explicit assignments here as structure assignment can copy */
  /* more than just the lsu (for small DECDPUN).  This would not affect */
  /* the value of the results, but would disturb test harness spill */
  /* checking. */
  dest->bits = src->bits;
  dest->exponent = src->exponent;
  dest->digits = src->digits;
  dest->lsu[0] = src->lsu[0];
  if (src->digits > DECDPUN)
    {				/* more Units to come */
      Unit *d;			/* work */
      const Unit *s, *smsup;	/* work */
      /* memcpy for the remaining Units would be safe as they cannot */
      /* overlap.  However, this explicit loop is faster in short cases. */
      d = dest->lsu + 1;	/* -> first destination */
      smsup = src->lsu + D2U (src->digits);	/* -> source msu+1 */
      for (s = src->lsu + 1; s < smsup; s++, d++)
	*d = *s;
    }
  return dest;
}

/* ------------------------------------------------------------------ */
/* decNumberTrim -- remove insignificant zeros                        */
/*                                                                    */
/*   dn is the number to trim                                         */
/*   returns dn                                                       */
/*                                                                    */
/* All fields are updated as required.  This is a utility operation,  */
/* so special values are unchanged and no error is possible.          */
/* ------------------------------------------------------------------ */
decNumber *
decNumberTrim (decNumber * dn)
{
  Int dropped;			/* work */
  return decTrim (dn, 0, &dropped);
}

/* ------------------------------------------------------------------ */
/* decNumberVersion -- return the name and version of this module     */
/*                                                                    */
/* No error is possible.                                              */
/* ------------------------------------------------------------------ */
const char *
decNumberVersion (void)
{
  return DECVERSION;
}

/* ------------------------------------------------------------------ */
/* decNumberZero -- set a number to 0                                 */
/*                                                                    */
/*   dn is the number to set, with space for one digit                */
/*   returns dn                                                       */
/*                                                                    */
/* No error is possible.                                              */
/* ------------------------------------------------------------------ */
/* Memset is not used as it is much slower in some environments. */
decNumber *
decNumberZero (decNumber * dn)
{

#if DECCHECK
  if (decCheckOperands (dn, DECUNUSED, DECUNUSED, DECUNUSED))
    return dn;
#endif

  dn->bits = 0;
  dn->exponent = 0;
  dn->digits = 1;
  dn->lsu[0] = 0;
  return dn;
}

/* ================================================================== */
/* Local routines                                                     */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* decToString -- lay out a number into a string                      */
/*                                                                    */
/*   dn     is the number to lay out                                  */
/*   string is where to lay out the number                            */
/*   eng    is 1 if Engineering, 0 if Scientific                      */
/*                                                                    */
/* str must be at least dn->digits+14 characters long                 */
/* No error is possible.                                              */
/*                                                                    */
/* Note that this routine can generate a -0 or 0.000.  These are      */
/* never generated in subset to-number or arithmetic, but can occur   */
/* in non-subset arithmetic (e.g., -1*0 or 1.234-1.234).              */
/* ------------------------------------------------------------------ */
/* If DECCHECK is enabled the string "?" is returned if a number is */
/* invalid. */

/* TODIGIT -- macro to remove the leading digit from the unsigned */
/* integer u at column cut (counting from the right, LSD=0) and place */
/* it as an ASCII character into the character pointed to by c.  Note */
/* that cut must be <= 9, and the maximum value for u is 2,000,000,000 */
/* (as is needed for negative exponents of subnormals).  The unsigned */
/* integer pow is used as a temporary variable. */
#define TODIGIT(u, cut, c) {            \
  *(c)='0';                             \
  pow=powers[cut]*2;                    \
  if ((u)>pow) {                        \
    pow*=4;                             \
    if ((u)>=pow) {(u)-=pow; *(c)+=8;}  \
    pow/=2;                             \
    if ((u)>=pow) {(u)-=pow; *(c)+=4;}  \
    pow/=2;                             \
    }                                   \
  if ((u)>=pow) {(u)-=pow; *(c)+=2;}    \
  pow/=2;                               \
  if ((u)>=pow) {(u)-=pow; *(c)+=1;}    \
  }

static void
decToString (const decNumber * dn, char *string, Flag eng)
{
  Int exp = dn->exponent;	/* local copy */
  Int e;			/* E-part value */
  Int pre;			/* digits before the '.' */
  Int cut;			/* for counting digits in a Unit */
  char *c = string;		/* work [output pointer] */
  const Unit *up = dn->lsu + D2U (dn->digits) - 1;	/* -> msu [input pointer] */
  uInt u, pow;			/* work */

#if DECCHECK
  if (decCheckOperands (DECUNUSED, dn, DECUNUSED, DECUNUSED))
    {
      strcpy (string, "?");
      return;
    }
#endif

  if (decNumberIsNegative (dn))
    {				/* Negatives get a minus (except */
      *c = '-';			/* NaNs, which remove the '-' below) */
      c++;
    }
  if (dn->bits & DECSPECIAL)
    {				/* Is a special value */
      if (decNumberIsInfinite (dn))
	{
	  strcpy (c, "Infinity");
	  return;
	}
      /* a NaN */
      if (dn->bits & DECSNAN)
	{			/* signalling NaN */
	  *c = 's';
	  c++;
	}
      strcpy (c, "NaN");
      c += 3;			/* step past */
      /* if not a clean non-zero coefficient, that's all we have in a */
      /* NaN string */
      if (exp != 0 || (*dn->lsu == 0 && dn->digits == 1))
	return;
      /* [drop through to add integer] */
    }

  /* calculate how many digits in msu, and hence first cut */
  cut = dn->digits % DECDPUN;
  if (cut == 0)
    cut = DECDPUN;		/* msu is full */
  cut--;			/* power of ten for digit */

  if (exp == 0)
    {				/* simple integer [common fastpath, */
      /*   used for NaNs, too] */
      for (; up >= dn->lsu; up--)
	{			/* each Unit from msu */
	  u = *up;		/* contains DECDPUN digits to lay out */
	  for (; cut >= 0; c++, cut--)
	    TODIGIT (u, cut, c);
	  cut = DECDPUN - 1;	/* next Unit has all digits */
	}
      *c = '\0';		/* terminate the string */
      return;
    }

  /* non-0 exponent -- assume plain form */
  pre = dn->digits + exp;	/* digits before '.' */
  e = 0;			/* no E */
  if ((exp > 0) || (pre < -5))
    {				/* need exponential form */
      e = exp + dn->digits - 1;	/* calculate E value */
      pre = 1;			/* assume one digit before '.' */
      if (eng && (e != 0))
	{			/* may need to adjust */
	  Int adj;		/* adjustment */
	  /* The C remainder operator is undefined for negative numbers, so */
	  /* we must use positive remainder calculation here */
	  if (e < 0)
	    {
	      adj = (-e) % 3;
	      if (adj != 0)
		adj = 3 - adj;
	    }
	  else
	    {			/* e>0 */
	      adj = e % 3;
	    }
	  e = e - adj;
	  /* if we are dealing with zero we will use exponent which is a */
	  /* multiple of three, as expected, but there will only be the */
	  /* one zero before the E, still.  Otherwise note the padding. */
	  if (!ISZERO (dn))
	    pre += adj;
	  else
	    {			/* is zero */
	      if (adj != 0)
		{		/* 0.00Esnn needed */
		  e = e + 3;
		  pre = -(2 - adj);
		}
	    }			/* zero */
	}			/* eng */
    }

  /* lay out the digits of the coefficient, adding 0s and . as needed */
  u = *up;
  if (pre > 0)
    {				/* xxx.xxx or xx00 (engineering) form */
      for (; pre > 0; pre--, c++, cut--)
	{
	  if (cut < 0)
	    {			/* need new Unit */
	      if (up == dn->lsu)
		break;		/* out of input digits (pre>digits) */
	      up--;
	      cut = DECDPUN - 1;
	      u = *up;
	    }
	  TODIGIT (u, cut, c);
	}
      if (up > dn->lsu || (up == dn->lsu && cut >= 0))
	{			/* more to come, after '.' */
	  *c = '.';
	  c++;
	  for (;; c++, cut--)
	    {
	      if (cut < 0)
		{		/* need new Unit */
		  if (up == dn->lsu)
		    break;	/* out of input digits */
		  up--;
		  cut = DECDPUN - 1;
		  u = *up;
		}
	      TODIGIT (u, cut, c);
	    }
	}
      else
	for (; pre > 0; pre--, c++)
	  *c = '0';		/* 0 padding (for engineering) needed */
    }
  else
    {				/* 0.xxx or 0.000xxx form */
      *c = '0';
      c++;
      *c = '.';
      c++;
      for (; pre < 0; pre++, c++)
	*c = '0';		/* add any 0's after '.' */
      for (;; c++, cut--)
	{
	  if (cut < 0)
	    {			/* need new Unit */
	      if (up == dn->lsu)
		break;		/* out of input digits */
	      up--;
	      cut = DECDPUN - 1;
	      u = *up;
	    }
	  TODIGIT (u, cut, c);
	}
    }

  /* Finally add the E-part, if needed.  It will never be 0, has a
     base maximum and minimum of +999999999 through -999999999, but
     could range down to -1999999998 for subnormal numbers */
  if (e != 0)
    {
      Flag had = 0;		/* 1=had non-zero */
      *c = 'E';
      c++;
      *c = '+';
      c++;			/* assume positive */
      u = e;			/* .. */
      if (e < 0)
	{
	  *(c - 1) = '-';	/* oops, need - */
	  u = -e;		/* uInt, please */
	}
      /* layout the exponent (_itoa is not ANSI C) */
      for (cut = 9; cut >= 0; cut--)
	{
	  TODIGIT (u, cut, c);
	  if (*c == '0' && !had)
	    continue;		/* skip leading zeros */
	  had = 1;		/* had non-0 */
	  c++;			/* step for next */
	}			/* cut */
    }
  *c = '\0';			/* terminate the string (all paths) */
  return;
}

/* ------------------------------------------------------------------ */
/* decAddOp -- add/subtract operation                                 */
/*                                                                    */
/*   This computes C = A + B                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X+X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*   negate is DECNEG if rhs should be negated, or 0 otherwise        */
/*   status accumulates status for the caller                         */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/* ------------------------------------------------------------------ */
/* If possible, we calculate the coefficient directly into C.         */
/* However, if:                                                       */
/*   -- we need a digits+1 calculation because numbers are unaligned  */
/*      and span more than set->digits digits                         */
/*   -- a carry to digits+1 digits looks possible                     */
/*   -- C is the same as A or B, and the result would destructively   */
/*      overlap the A or B coefficient                                */
/* then we must calculate into a temporary buffer.  In this latter    */
/* case we use the local (stack) buffer if possible, and only if too  */
/* long for that do we resort to malloc.                              */
/*                                                                    */
/* Misalignment is handled as follows:                                */
/*   Apad: (AExp>BExp) Swap operands and proceed as for BExp>AExp.    */
/*   BPad: Apply the padding by a combination of shifting (whole      */
/*         units) and multiplication (part units).                    */
/*                                                                    */
/* Addition, especially x=x+1, is speed-critical, so we take pains    */
/* to make returning as fast as possible, by flagging any allocation. */
/* ------------------------------------------------------------------ */
static decNumber *
decAddOp (decNumber * res, const decNumber * lhs,
	  const decNumber * rhs, decContext * set, uByte negate, uInt * status)
{
  decNumber *alloclhs = NULL;	/* non-NULL if rounded lhs allocated */
  decNumber *allocrhs = NULL;	/* .., rhs */
  Int rhsshift;			/* working shift (in Units) */
  Int maxdigits;		/* longest logical length */
  Int mult;			/* multiplier */
  Int residue;			/* rounding accumulator */
  uByte bits;			/* result bits */
  Flag diffsign;		/* non-0 if arguments have different sign */
  Unit *acc;			/* accumulator for result */
  Unit accbuff[D2U (DECBUFFER + 1)];	/* local buffer [+1 is for possible */
  /* final carry digit or DECBUFFER=0] */
  Unit *allocacc = NULL;	/* -> allocated acc buffer, iff allocated */
  Flag alloced = 0;		/* set non-0 if any allocations */
  Int reqdigits = set->digits;	/* local copy; requested DIGITS */
  uByte merged;			/* merged flags */
  Int padding;			/* work */

#if DECCHECK
  if (decCheckOperands (res, lhs, rhs, set))
    return res;
#endif

  do
    {				/* protect allocated storage */
#if DECSUBSET
      if (!set->extended)
	{
	  /* reduce operands and set lostDigits status, as needed */
	  if (lhs->digits > reqdigits)
	    {
	      alloclhs = decRoundOperand (lhs, set, status);
	      if (alloclhs == NULL)
		break;
	      lhs = alloclhs;
	      alloced = 1;
	    }
	  if (rhs->digits > reqdigits)
	    {
	      allocrhs = decRoundOperand (rhs, set, status);
	      if (allocrhs == NULL)
		break;
	      rhs = allocrhs;
	      alloced = 1;
	    }
	}
#endif
      /* [following code does not require input rounding] */

      /* note whether signs differ */
      diffsign = (Flag) ((lhs->bits ^ rhs->bits ^ negate) & DECNEG);

      /* handle infinities and NaNs */
      merged = (lhs->bits | rhs->bits) & DECSPECIAL;
      if (merged)
	{			/* a special bit set */
	  if (merged & (DECSNAN | DECNAN))	/* a NaN */
	    decNaNs (res, lhs, rhs, status);
	  else
	    {			/* one or two infinities */
	      if (decNumberIsInfinite (lhs))
		{		/* LHS is infinity */
		  /* two infinities with different signs is invalid */
		  if (decNumberIsInfinite (rhs) && diffsign)
		    {
		      *status |= DEC_Invalid_operation;
		      break;
		    }
		  bits = lhs->bits & DECNEG;	/* get sign from LHS */
		}
	      else
		bits = (rhs->bits ^ negate) & DECNEG;	/* RHS must be Infinity */
	      bits |= DECINF;
	      decNumberZero (res);
	      res->bits = bits;	/* set +/- infinity */
	    }			/* an infinity */
	  break;
	}

      /* Quick exit for add 0s; return the non-0, modified as need be */
      if (ISZERO (lhs))
	{
	  Int adjust;		/* work */
	  Int lexp = lhs->exponent;	/* save in case LHS==RES */
	  bits = lhs->bits;	/* .. */
	  residue = 0;		/* clear accumulator */
	  decCopyFit (res, rhs, set, &residue, status);	/* copy (as needed) */
	  res->bits ^= negate;	/* flip if rhs was negated */
#if DECSUBSET
	  if (set->extended)
	    {			/* exponents on zeros count */
#endif
	      /* exponent will be the lower of the two */
	      adjust = lexp - res->exponent;	/* adjustment needed [if -ve] */
	      if (ISZERO (res))
		{		/* both 0: special IEEE 854 rules */
		  if (adjust < 0)
		    res->exponent = lexp;	/* set exponent */
		  /* 0-0 gives +0 unless rounding to -infinity, and -0-0 gives -0 */
		  if (diffsign)
		    {
		      if (set->round != DEC_ROUND_FLOOR)
			res->bits = 0;
		      else
			res->bits = DECNEG;	/* preserve 0 sign */
		    }
		}
	      else
		{		/* non-0 res */
		  if (adjust < 0)
		    {		/* 0-padding needed */
		      if ((res->digits - adjust) > set->digits)
			{
			  adjust = res->digits - set->digits;	/* to fit exactly */
			  *status |= DEC_Rounded;	/* [but exact] */
			}
		      res->digits =
			decShiftToMost (res->lsu, res->digits, -adjust);
		      res->exponent += adjust;	/* set the exponent. */
		    }
		}		/* non-0 res */
#if DECSUBSET
	    }			/* extended */
#endif
	  decFinish (res, set, &residue, status);	/* clean and finalize */
	  break;
	}

      if (ISZERO (rhs))
	{			/* [lhs is non-zero] */
	  Int adjust;		/* work */
	  Int rexp = rhs->exponent;	/* save in case RHS==RES */
	  bits = rhs->bits;	/* be clean */
	  residue = 0;		/* clear accumulator */
	  decCopyFit (res, lhs, set, &residue, status);	/* copy (as needed) */
#if DECSUBSET
	  if (set->extended)
	    {			/* exponents on zeros count */
#endif
	      /* exponent will be the lower of the two */
	      /* [0-0 case handled above] */
	      adjust = rexp - res->exponent;	/* adjustment needed [if -ve] */
	      if (adjust < 0)
		{		/* 0-padding needed */
		  if ((res->digits - adjust) > set->digits)
		    {
		      adjust = res->digits - set->digits;	/* to fit exactly */
		      *status |= DEC_Rounded;	/* [but exact] */
		    }
		  res->digits =
		    decShiftToMost (res->lsu, res->digits, -adjust);
		  res->exponent += adjust;	/* set the exponent. */
		}
#if DECSUBSET
	    }			/* extended */
#endif
	  decFinish (res, set, &residue, status);	/* clean and finalize */
	  break;
	}
      /* [both fastpath and mainpath code below assume these cases */
      /* (notably 0-0) have already been handled] */

      /* calculate the padding needed to align the operands */
      padding = rhs->exponent - lhs->exponent;

      /* Fastpath cases where the numbers are aligned and normal, the RHS */
      /* is all in one unit, no operand rounding is needed, and no carry, */
      /* lengthening, or borrow is needed */
      if (rhs->digits <= DECDPUN && padding == 0 && rhs->exponent >= set->emin	/* [some normals drop through] */
	  && rhs->digits <= reqdigits && lhs->digits <= reqdigits)
	{
	  Int partial = *lhs->lsu;
	  if (!diffsign)
	    {			/* adding */
	      Int maxv = DECDPUNMAX;	/* highest no-overflow */
	      if (lhs->digits < DECDPUN)
		maxv = powers[lhs->digits] - 1;
	      partial += *rhs->lsu;
	      if (partial <= maxv)
		{		/* no carry */
		  if (res != lhs)
		    decNumberCopy (res, lhs);	/* not in place */
		  *res->lsu = (Unit) partial;	/* [copy could have overwritten RHS] */
		  break;
		}
	      /* else drop out for careful add */
	    }
	  else
	    {			/* signs differ */
	      partial -= *rhs->lsu;
	      if (partial > 0)
		{		/* no borrow needed, and non-0 result */
		  if (res != lhs)
		    decNumberCopy (res, lhs);	/* not in place */
		  *res->lsu = (Unit) partial;
		  /* this could have reduced digits [but result>0] */
		  res->digits = decGetDigits (res->lsu, D2U (res->digits));
		  break;
		}
	      /* else drop out for careful subtract */
	    }
	}

      /* Now align (pad) the lhs or rhs so we can add or subtract them, as
         necessary.  If one number is much larger than the other (that is,
         if in plain form there is a least one digit between the lowest
         digit or one and the highest of the other) we need to pad with up
         to DIGITS-1 trailing zeros, and then apply rounding (as exotic
         rounding modes may be affected by the residue).
       */
      rhsshift = 0;		/* rhs shift to left (padding) in Units */
      bits = lhs->bits;		/* assume sign is that of LHS */
      mult = 1;			/* likely multiplier */

      /* if padding==0 the operands are aligned; no padding needed */
      if (padding != 0)
	{
	  /* some padding needed */
	  /* We always pad the RHS, as we can then effect any required */
	  /* padding by a combination of shifts and a multiply */
	  Flag swapped = 0;
	  if (padding < 0)
	    {			/* LHS needs the padding */
	      const decNumber *t;
	      padding = -padding;	/* will be +ve */
	      bits = (uByte) (rhs->bits ^ negate);	/* assumed sign is now that of RHS */
	      t = lhs;
	      lhs = rhs;
	      rhs = t;
	      swapped = 1;
	    }

	  /* If, after pad, rhs would be longer than lhs by digits+1 or */
	  /* more then lhs cannot affect the answer, except as a residue, */
	  /* so we only need to pad up to a length of DIGITS+1. */
	  if (rhs->digits + padding > lhs->digits + reqdigits + 1)
	    {
	      /* The RHS is sufficient */
	      /* for residue we use the relative sign indication... */
	      Int shift = reqdigits - rhs->digits;	/* left shift needed */
	      residue = 1;	/* residue for rounding */
	      if (diffsign)
		residue = -residue;	/* signs differ */
	      /* copy, shortening if necessary */
	      decCopyFit (res, rhs, set, &residue, status);
	      /* if it was already shorter, then need to pad with zeros */
	      if (shift > 0)
		{
		  res->digits = decShiftToMost (res->lsu, res->digits, shift);
		  res->exponent -= shift;	/* adjust the exponent. */
		}
	      /* flip the result sign if unswapped and rhs was negated */
	      if (!swapped)
		res->bits ^= negate;
	      decFinish (res, set, &residue, status);	/* done */
	      break;
	    }

	  /* LHS digits may affect result */
	  rhsshift = D2U (padding + 1) - 1;	/* this much by Unit shift .. */
	  mult = powers[padding - (rhsshift * DECDPUN)];	/* .. this by multiplication */
	}			/* padding needed */

      if (diffsign)
	mult = -mult;		/* signs differ */

      /* determine the longer operand */
      maxdigits = rhs->digits + padding;	/* virtual length of RHS */
      if (lhs->digits > maxdigits)
	maxdigits = lhs->digits;

      /* Decide on the result buffer to use; if possible place directly */
      /* into result. */
      acc = res->lsu;		/* assume build direct */
      /* If destructive overlap, or the number is too long, or a carry or */
      /* borrow to DIGITS+1 might be possible we must use a buffer. */
      /* [Might be worth more sophisticated tests when maxdigits==reqdigits] */
      if ((maxdigits >= reqdigits)	/* is, or could be, too large */
	  || (res == rhs && rhsshift > 0))
	{			/* destructive overlap */
	  /* buffer needed; choose it */
	  /* we'll need units for maxdigits digits, +1 Unit for carry or borrow */
	  Int need = D2U (maxdigits) + 1;
	  acc = accbuff;	/* assume use local buffer */
	  if (need * sizeof (Unit) > sizeof (accbuff))
	    {
	      allocacc = (Unit *) malloc (need * sizeof (Unit));
	      if (allocacc == NULL)
		{		/* hopeless -- abandon */
		  *status |= DEC_Insufficient_storage;
		  break;
		}
	      acc = allocacc;
	      alloced = 1;
	    }
	}

      res->bits = (uByte) (bits & DECNEG);	/* it's now safe to overwrite.. */
      res->exponent = lhs->exponent;	/* .. operands (even if aliased) */

#if DECTRACE
      decDumpAr ('A', lhs->lsu, D2U (lhs->digits));
      decDumpAr ('B', rhs->lsu, D2U (rhs->digits));
      printf ("  :h: %d %d\n", rhsshift, mult);
#endif

      /* add [A+B*m] or subtract [A+B*(-m)] */
      res->digits = decUnitAddSub (lhs->lsu, D2U (lhs->digits), rhs->lsu, D2U (rhs->digits), rhsshift, acc, mult) * DECDPUN;	/* [units -> digits] */
      if (res->digits < 0)
	{			/* we borrowed */
	  res->digits = -res->digits;
	  res->bits ^= DECNEG;	/* flip the sign */
	}
#if DECTRACE
      decDumpAr ('+', acc, D2U (res->digits));
#endif

      /* If we used a buffer we need to copy back, possibly shortening */
      /* (If we didn't use buffer it must have fit, so can't need rounding */
      /* and residue must be 0.) */
      residue = 0;		/* clear accumulator */
      if (acc != res->lsu)
	{
#if DECSUBSET
	  if (set->extended)
	    {			/* round from first significant digit */
#endif
	      /* remove leading zeros that we added due to rounding up to */
	      /* integral Units -- before the test for rounding. */
	      if (res->digits > reqdigits)
		res->digits = decGetDigits (acc, D2U (res->digits));
	      decSetCoeff (res, set, acc, res->digits, &residue, status);
#if DECSUBSET
	    }
	  else
	    {			/* subset arithmetic rounds from original significant digit */
	      /* We may have an underestimate.  This only occurs when both */
	      /* numbers fit in DECDPUN digits and we are padding with a */
	      /* negative multiple (-10, -100...) and the top digit(s) become */
	      /* 0.  (This only matters if we are using X3.274 rules where the */
	      /* leading zero could be included in the rounding.) */
	      if (res->digits < maxdigits)
		{
		  *(acc + D2U (res->digits)) = 0;	/* ensure leading 0 is there */
		  res->digits = maxdigits;
		}
	      else
		{
		  /* remove leading zeros that we added due to rounding up to */
		  /* integral Units (but only those in excess of the original */
		  /* maxdigits length, unless extended) before test for rounding. */
		  if (res->digits > reqdigits)
		    {
		      res->digits = decGetDigits (acc, D2U (res->digits));
		      if (res->digits < maxdigits)
			res->digits = maxdigits;
		    }
		}
	      decSetCoeff (res, set, acc, res->digits, &residue, status);
	      /* Now apply rounding if needed before removing leading zeros. */
	      /* This is safe because subnormals are not a possibility */
	      if (residue != 0)
		{
		  decApplyRound (res, set, residue, status);
		  residue = 0;	/* we did what we had to do */
		}
	    }			/* subset */
#endif
	}			/* used buffer */

      /* strip leading zeros [these were left on in case of subset subtract] */
      res->digits = decGetDigits (res->lsu, D2U (res->digits));

      /* apply checks and rounding */
      decFinish (res, set, &residue, status);

      /* "When the sum of two operands with opposite signs is exactly */
      /* zero, the sign of that sum shall be '+' in all rounding modes */
      /* except round toward -Infinity, in which mode that sign shall be */
      /* '-'."  [Subset zeros also never have '-', set by decFinish.] */
      if (ISZERO (res) && diffsign
#if DECSUBSET
	  && set->extended
#endif
	  && (*status & DEC_Inexact) == 0)
	{
	  if (set->round == DEC_ROUND_FLOOR)
	    res->bits |= DECNEG;	/* sign - */
	  else
	    res->bits &= ~DECNEG;	/* sign + */
	}
    }
  while (0);			/* end protected */

  if (alloced)
    {
      if (allocacc != NULL)
	free (allocacc);	/* drop any storage we used */
      if (allocrhs != NULL)
	free (allocrhs);	/* .. */
      if (alloclhs != NULL)
	free (alloclhs);	/* .. */
    }
  return res;
}

/* ------------------------------------------------------------------ */
/* decDivideOp -- division operation                                  */
/*                                                                    */
/*  This routine performs the calculations for all four division      */
/*  operators (divide, divideInteger, remainder, remainderNear).      */
/*                                                                    */
/*  C=A op B                                                          */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X/X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*   op  is DIVIDE, DIVIDEINT, REMAINDER, or REMNEAR respectively.    */
/*   status is the usual accumulator                                  */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* ------------------------------------------------------------------ */
/*   The underlying algorithm of this routine is the same as in the   */
/*   1981 S/370 implementation, that is, non-restoring long division  */
/*   with bi-unit (rather than bi-digit) estimation for each unit     */
/*   multiplier.  In this pseudocode overview, complications for the  */
/*   Remainder operators and division residues for exact rounding are */
/*   omitted for clarity.                                             */
/*                                                                    */
/*     Prepare operands and handle special values                     */
/*     Test for x/0 and then 0/x                                      */
/*     Exp =Exp1 - Exp2                                               */
/*     Exp =Exp +len(var1) -len(var2)                                 */
/*     Sign=Sign1 * Sign2                                             */
/*     Pad accumulator (Var1) to double-length with 0's (pad1)        */
/*     Pad Var2 to same length as Var1                                */
/*     msu2pair/plus=1st 2 or 1 units of var2, +1 to allow for round  */
/*     have=0                                                         */
/*     Do until (have=digits+1 OR residue=0)                          */
/*       if exp<0 then if integer divide/residue then leave           */
/*       this_unit=0                                                  */
/*       Do forever                                                   */
/*          compare numbers                                           */
/*          if <0 then leave inner_loop                               */
/*          if =0 then (* quick exit without subtract *) do           */
/*             this_unit=this_unit+1; output this_unit                */
/*             leave outer_loop; end                                  */
/*          Compare lengths of numbers (mantissae):                   */
/*          If same then tops2=msu2pair -- {units 1&2 of var2}        */
/*                  else tops2=msu2plus -- {0, unit 1 of var2}        */
/*          tops1=first_unit_of_Var1*10**DECDPUN +second_unit_of_var1 */
/*          mult=tops1/tops2  -- Good and safe guess at divisor       */
/*          if mult=0 then mult=1                                     */
/*          this_unit=this_unit+mult                                  */
/*          subtract                                                  */
/*          end inner_loop                                            */
/*        if have\=0 | this_unit\=0 then do                           */
/*          output this_unit                                          */
/*          have=have+1; end                                          */
/*        var2=var2/10                                                */
/*        exp=exp-1                                                   */
/*        end outer_loop                                              */
/*     exp=exp+1   -- set the proper exponent                         */
/*     if have=0 then generate answer=0                               */
/*     Return (Result is defined by Var1)                             */
/*                                                                    */
/* ------------------------------------------------------------------ */
/* We need two working buffers during the long division; one (digits+ */
/* 1) to accumulate the result, and the other (up to 2*digits+1) for  */
/* long subtractions.  These are acc and var1 respectively.           */
/* var1 is a copy of the lhs coefficient, var2 is the rhs coefficient.*/
/* ------------------------------------------------------------------ */
static decNumber *
decDivideOp (decNumber * res,
	     const decNumber * lhs, const decNumber * rhs,
	     decContext * set, Flag op, uInt * status)
{
  decNumber *alloclhs = NULL;	/* non-NULL if rounded lhs allocated */
  decNumber *allocrhs = NULL;	/* .., rhs */
  Unit accbuff[D2U (DECBUFFER + DECDPUN)];	/* local buffer */
  Unit *acc = accbuff;		/* -> accumulator array for result */
  Unit *allocacc = NULL;	/* -> allocated buffer, iff allocated */
  Unit *accnext;		/* -> where next digit will go */
  Int acclength;		/* length of acc needed [Units] */
  Int accunits;			/* count of units accumulated */
  Int accdigits;		/* count of digits accumulated */

  Unit varbuff[D2U (DECBUFFER * 2 + DECDPUN) * sizeof (Unit)];	/* buffer for var1 */
  Unit *var1 = varbuff;		/* -> var1 array for long subtraction */
  Unit *varalloc = NULL;	/* -> allocated buffer, iff used */

  const Unit *var2;		/* -> var2 array */

  Int var1units, var2units;	/* actual lengths */
  Int var2ulen;			/* logical length (units) */
  Int var1initpad = 0;		/* var1 initial padding (digits) */
  Unit *msu1;			/* -> msu of each var */
  const Unit *msu2;		/* -> msu of each var */
  Int msu2plus;			/* msu2 plus one [does not vary] */
  eInt msu2pair;		/* msu2 pair plus one [does not vary] */
  Int maxdigits;		/* longest LHS or required acc length */
  Int mult;			/* multiplier for subtraction */
  Unit thisunit;		/* current unit being accumulated */
  Int residue;			/* for rounding */
  Int reqdigits = set->digits;	/* requested DIGITS */
  Int exponent;			/* working exponent */
  Int maxexponent = 0;		/* DIVIDE maximum exponent if unrounded */
  uByte bits;			/* working sign */
  uByte merged;			/* merged flags */
  Unit *target;			/* work */
  const Unit *source;		/* work */
  uInt const *pow;		/* .. */
  Int shift, cut;		/* .. */
#if DECSUBSET
  Int dropped;			/* work */
#endif

#if DECCHECK
  if (decCheckOperands (res, lhs, rhs, set))
    return res;
#endif

  do
    {				/* protect allocated storage */
#if DECSUBSET
      if (!set->extended)
	{
	  /* reduce operands and set lostDigits status, as needed */
	  if (lhs->digits > reqdigits)
	    {
	      alloclhs = decRoundOperand (lhs, set, status);
	      if (alloclhs == NULL)
		break;
	      lhs = alloclhs;
	    }
	  if (rhs->digits > reqdigits)
	    {
	      allocrhs = decRoundOperand (rhs, set, status);
	      if (allocrhs == NULL)
		break;
	      rhs = allocrhs;
	    }
	}
#endif
      /* [following code does not require input rounding] */

      bits = (lhs->bits ^ rhs->bits) & DECNEG;	/* assumed sign for divisions */

      /* handle infinities and NaNs */
      merged = (lhs->bits | rhs->bits) & DECSPECIAL;
      if (merged)
	{			/* a special bit set */
	  if (merged & (DECSNAN | DECNAN))
	    {			/* one or two NaNs */
	      decNaNs (res, lhs, rhs, status);
	      break;
	    }
	  /* one or two infinities */
	  if (decNumberIsInfinite (lhs))
	    {			/* LHS (dividend) is infinite */
	      if (decNumberIsInfinite (rhs) ||	/* two infinities are invalid .. */
		  op & (REMAINDER | REMNEAR))
		{		/* as is remainder of infinity */
		  *status |= DEC_Invalid_operation;
		  break;
		}
	      /* [Note that infinity/0 raises no exceptions] */
	      decNumberZero (res);
	      res->bits = bits | DECINF;	/* set +/- infinity */
	      break;
	    }
	  else
	    {			/* RHS (divisor) is infinite */
	      residue = 0;
	      if (op & (REMAINDER | REMNEAR))
		{
		  /* result is [finished clone of] lhs */
		  decCopyFit (res, lhs, set, &residue, status);
		}
	      else
		{		/* a division */
		  decNumberZero (res);
		  res->bits = bits;	/* set +/- zero */
		  /* for DIVIDEINT the exponent is always 0.  For DIVIDE, result */
		  /* is a 0 with infinitely negative exponent, clamped to minimum */
		  if (op & DIVIDE)
		    {
		      res->exponent = set->emin - set->digits + 1;
		      *status |= DEC_Clamped;
		    }
		}
	      decFinish (res, set, &residue, status);
	      break;
	    }
	}

      /* handle 0 rhs (x/0) */
      if (ISZERO (rhs))
	{			/* x/0 is always exceptional */
	  if (ISZERO (lhs))
	    {
	      decNumberZero (res);	/* [after lhs test] */
	      *status |= DEC_Division_undefined;	/* 0/0 will become NaN */
	    }
	  else
	    {
	      decNumberZero (res);
	      if (op & (REMAINDER | REMNEAR))
		*status |= DEC_Invalid_operation;
	      else
		{
		  *status |= DEC_Division_by_zero;	/* x/0 */
		  res->bits = bits | DECINF;	/* .. is +/- Infinity */
		}
	    }
	  break;
	}

      /* handle 0 lhs (0/x) */
      if (ISZERO (lhs))
	{			/* 0/x [x!=0] */
#if DECSUBSET
	  if (!set->extended)
	    decNumberZero (res);
	  else
	    {
#endif
	      if (op & DIVIDE)
		{
		  residue = 0;
		  exponent = lhs->exponent - rhs->exponent;	/* ideal exponent */
		  decNumberCopy (res, lhs);	/* [zeros always fit] */
		  res->bits = bits;	/* sign as computed */
		  res->exponent = exponent;	/* exponent, too */
		  decFinalize (res, set, &residue, status);	/* check exponent */
		}
	      else if (op & DIVIDEINT)
		{
		  decNumberZero (res);	/* integer 0 */
		  res->bits = bits;	/* sign as computed */
		}
	      else
		{		/* a remainder */
		  exponent = rhs->exponent;	/* [save in case overwrite] */
		  decNumberCopy (res, lhs);	/* [zeros always fit] */
		  if (exponent < res->exponent)
		    res->exponent = exponent;	/* use lower */
		}
#if DECSUBSET
	    }
#endif
	  break;
	}

      /* Precalculate exponent.  This starts off adjusted (and hence fits */
      /* in 31 bits) and becomes the usual unadjusted exponent as the */
      /* division proceeds.  The order of evaluation is important, here, */
      /* to avoid wrap. */
      exponent =
	(lhs->exponent + lhs->digits) - (rhs->exponent + rhs->digits);

      /* If the working exponent is -ve, then some quick exits are */
      /* possible because the quotient is known to be <1 */
      /* [for REMNEAR, it needs to be < -1, as -0.5 could need work] */
      if (exponent < 0 && !(op == DIVIDE))
	{
	  if (op & DIVIDEINT)
	    {
	      decNumberZero (res);	/* integer part is 0 */
#if DECSUBSET
	      if (set->extended)
#endif
		res->bits = bits;	/* set +/- zero */
	      break;
	    }
	  /* we can fastpath remainders so long as the lhs has the */
	  /* smaller (or equal) exponent */
	  if (lhs->exponent <= rhs->exponent)
	    {
	      if (op & REMAINDER || exponent < -1)
		{
		  /* It is REMAINDER or safe REMNEAR; result is [finished */
		  /* clone of] lhs  (r = x - 0*y) */
		  residue = 0;
		  decCopyFit (res, lhs, set, &residue, status);
		  decFinish (res, set, &residue, status);
		  break;
		}
	      /* [unsafe REMNEAR drops through] */
	    }
	}			/* fastpaths */

      /* We need long (slow) division; roll up the sleeves... */

      /* The accumulator will hold the quotient of the division. */
      /* If it needs to be too long for stack storage, then allocate. */
      acclength = D2U (reqdigits + DECDPUN);	/* in Units */
      if (acclength * sizeof (Unit) > sizeof (accbuff))
	{
	  allocacc = (Unit *) malloc (acclength * sizeof (Unit));
	  if (allocacc == NULL)
	    {			/* hopeless -- abandon */
	      *status |= DEC_Insufficient_storage;
	      break;
	    }
	  acc = allocacc;	/* use the allocated space */
	}

      /* var1 is the padded LHS ready for subtractions. */
      /* If it needs to be too long for stack storage, then allocate. */
      /* The maximum units we need for var1 (long subtraction) is: */
      /* Enough for */
      /*     (rhs->digits+reqdigits-1) -- to allow full slide to right */
      /* or  (lhs->digits)             -- to allow for long lhs */
      /* whichever is larger */
      /*   +1                -- for rounding of slide to right */
      /*   +1                -- for leading 0s */
      /*   +1                -- for pre-adjust if a remainder or DIVIDEINT */
      /* [Note: unused units do not participate in decUnitAddSub data] */
      maxdigits = rhs->digits + reqdigits - 1;
      if (lhs->digits > maxdigits)
	maxdigits = lhs->digits;
      var1units = D2U (maxdigits) + 2;
      /* allocate a guard unit above msu1 for REMAINDERNEAR */
      if (!(op & DIVIDE))
	var1units++;
      if ((var1units + 1) * sizeof (Unit) > sizeof (varbuff))
	{
	  varalloc = (Unit *) malloc ((var1units + 1) * sizeof (Unit));
	  if (varalloc == NULL)
	    {			/* hopeless -- abandon */
	      *status |= DEC_Insufficient_storage;
	      break;
	    }
	  var1 = varalloc;	/* use the allocated space */
	}

      /* Extend the lhs and rhs to full long subtraction length.  The lhs */
      /* is truly extended into the var1 buffer, with 0 padding, so we can */
      /* subtract in place.  The rhs (var2) has virtual padding */
      /* (implemented by decUnitAddSub). */
      /* We allocated one guard unit above msu1 for rem=rem+rem in REMAINDERNEAR */
      msu1 = var1 + var1units - 1;	/* msu of var1 */
      source = lhs->lsu + D2U (lhs->digits) - 1;	/* msu of input array */
      for (target = msu1; source >= lhs->lsu; source--, target--)
	*target = *source;
      for (; target >= var1; target--)
	*target = 0;

      /* rhs (var2) is left-aligned with var1 at the start */
      var2ulen = var1units;	/* rhs logical length (units) */
      var2units = D2U (rhs->digits);	/* rhs actual length (units) */
      var2 = rhs->lsu;		/* -> rhs array */
      msu2 = var2 + var2units - 1;	/* -> msu of var2 [never changes] */
      /* now set up the variables which we'll use for estimating the */
      /* multiplication factor.  If these variables are not exact, we add */
      /* 1 to make sure that we never overestimate the multiplier. */
      msu2plus = *msu2;		/* it's value .. */
      if (var2units > 1)
	msu2plus++;		/* .. +1 if any more */
      msu2pair = (eInt) * msu2 * (DECDPUNMAX + 1);	/* top two pair .. */
      if (var2units > 1)
	{			/* .. [else treat 2nd as 0] */
	  msu2pair += *(msu2 - 1);	/* .. */
	  if (var2units > 2)
	    msu2pair++;		/* .. +1 if any more */
	}

      /* Since we are working in units, the units may have leading zeros, */
      /* but we calculated the exponent on the assumption that they are */
      /* both left-aligned.  Adjust the exponent to compensate: add the */
      /* number of leading zeros in var1 msu and subtract those in var2 msu. */
      /* [We actually do this by counting the digits and negating, as */
      /* lead1=DECDPUN-digits1, and similarly for lead2.] */
      for (pow = &powers[1]; *msu1 >= *pow; pow++)
	exponent--;
      for (pow = &powers[1]; *msu2 >= *pow; pow++)
	exponent++;

      /* Now, if doing an integer divide or remainder, we want to ensure */
      /* that the result will be Unit-aligned.  To do this, we shift the */
      /* var1 accumulator towards least if need be.  (It's much easier to */
      /* do this now than to reassemble the residue afterwards, if we are */
      /* doing a remainder.)  Also ensure the exponent is not negative. */
      if (!(op & DIVIDE))
	{
	  Unit *u;
	  /* save the initial 'false' padding of var1, in digits */
	  var1initpad = (var1units - D2U (lhs->digits)) * DECDPUN;
	  /* Determine the shift to do. */
	  if (exponent < 0)
	    cut = -exponent;
	  else
	    cut = DECDPUN - exponent % DECDPUN;
	  decShiftToLeast (var1, var1units, cut);
	  exponent += cut;	/* maintain numerical value */
	  var1initpad -= cut;	/* .. and reduce padding */
	  /* clean any most-significant units we just emptied */
	  for (u = msu1; cut >= DECDPUN; cut -= DECDPUN, u--)
	    *u = 0;
	}			/* align */
      else
	{			/* is DIVIDE */
	  maxexponent = lhs->exponent - rhs->exponent;	/* save */
	  /* optimization: if the first iteration will just produce 0, */
	  /* preadjust to skip it [valid for DIVIDE only] */
	  if (*msu1 < *msu2)
	    {
	      var2ulen--;	/* shift down */
	      exponent -= DECDPUN;	/* update the exponent */
	    }
	}

      /* ---- start the long-division loops ------------------------------ */
      accunits = 0;		/* no units accumulated yet */
      accdigits = 0;		/* .. or digits */
      accnext = acc + acclength - 1;	/* -> msu of acc [NB: allows digits+1] */
      for (;;)
	{			/* outer forever loop */
	  thisunit = 0;		/* current unit assumed 0 */
	  /* find the next unit */
	  for (;;)
	    {			/* inner forever loop */
	      /* strip leading zero units [from either pre-adjust or from */
	      /* subtract last time around].  Leave at least one unit. */
	      for (; *msu1 == 0 && msu1 > var1; msu1--)
		var1units--;

	      if (var1units < var2ulen)
		break;		/* var1 too low for subtract */
	      if (var1units == var2ulen)
		{		/* unit-by-unit compare needed */
		  /* compare the two numbers, from msu */
		  Unit *pv1, v2;	/* units to compare */
		  const Unit *pv2;	/* units to compare */
		  pv2 = msu2;	/* -> msu */
		  for (pv1 = msu1;; pv1--, pv2--)
		    {
		      /* v1=*pv1 -- always OK */
		      v2 = 0;	/* assume in padding */
		      if (pv2 >= var2)
			v2 = *pv2;	/* in range */
		      if (*pv1 != v2)
			break;	/* no longer the same */
		      if (pv1 == var1)
			break;	/* done; leave pv1 as is */
		    }
		  /* here when all inspected or a difference seen */
		  if (*pv1 < v2)
		    break;	/* var1 too low to subtract */
		  if (*pv1 == v2)
		    {		/* var1 == var2 */
		      /* reach here if var1 and var2 are identical; subtraction */
		      /* would increase digit by one, and the residue will be 0 so */
		      /* we are done; leave the loop with residue set to 0. */
		      thisunit++;	/* as though subtracted */
		      *var1 = 0;	/* set var1 to 0 */
		      var1units = 1;	/* .. */
		      break;	/* from inner */
		    }		/* var1 == var2 */
		  /* *pv1>v2.  Prepare for real subtraction; the lengths are equal */
		  /* Estimate the multiplier (there's always a msu1-1)... */
		  /* Bring in two units of var2 to provide a good estimate. */
		  mult =
		    (Int) (((eInt) * msu1 * (DECDPUNMAX + 1) +
			    *(msu1 - 1)) / msu2pair);
		}		/* lengths the same */
	      else
		{		/* var1units > var2ulen, so subtraction is safe */
		  /* The var2 msu is one unit towards the lsu of the var1 msu, */
		  /* so we can only use one unit for var2. */
		  mult =
		    (Int) (((eInt) * msu1 * (DECDPUNMAX + 1) +
			    *(msu1 - 1)) / msu2plus);
		}
	      if (mult == 0)
		mult = 1;	/* must always be at least 1 */
	      /* subtraction needed; var1 is > var2 */
	      thisunit = (Unit) (thisunit + mult);	/* accumulate */
	      /* subtract var1-var2, into var1; only the overlap needs */
	      /* processing, as we are in place */
	      shift = var2ulen - var2units;
#if DECTRACE
	      decDumpAr ('1', &var1[shift], var1units - shift);
	      decDumpAr ('2', var2, var2units);
	      printf ("m=%d\n", -mult);
#endif
	      decUnitAddSub (&var1[shift], var1units - shift,
			     var2, var2units, 0, &var1[shift], -mult);
#if DECTRACE
	      decDumpAr ('#', &var1[shift], var1units - shift);
#endif
	      /* var1 now probably has leading zeros; these are removed at the */
	      /* top of the inner loop. */
	    }			/* inner loop */

	  /* We have the next unit; unless it's a leading zero, add to acc */
	  if (accunits != 0 || thisunit != 0)
	    {			/* put the unit we got */
	      *accnext = thisunit;	/* store in accumulator */
	      /* account exactly for the digits we got */
	      if (accunits == 0)
		{
		  accdigits++;	/* at least one */
		  for (pow = &powers[1]; thisunit >= *pow; pow++)
		    accdigits++;
		}
	      else
		accdigits += DECDPUN;
	      accunits++;	/* update count */
	      accnext--;	/* ready for next */
	      if (accdigits > reqdigits)
		break;		/* we have all we need */
	    }

	  /* if the residue is zero, we're done (unless divide or */
	  /* divideInteger and we haven't got enough digits yet) */
	  if (*var1 == 0 && var1units == 1)
	    {			/* residue is 0 */
	      if (op & (REMAINDER | REMNEAR))
		break;
	      if ((op & DIVIDE) && (exponent <= maxexponent))
		break;
	      /* [drop through if divideInteger] */
	    }
	  /* we've also done enough if calculating remainder or integer */
	  /* divide and we just did the last ('units') unit */
	  if (exponent == 0 && !(op & DIVIDE))
	    break;

	  /* to get here, var1 is less than var2, so divide var2 by the per- */
	  /* Unit power of ten and go for the next digit */
	  var2ulen--;		/* shift down */
	  exponent -= DECDPUN;	/* update the exponent */
	}			/* outer loop */

      /* ---- division is complete --------------------------------------- */
      /* here: acc      has at least reqdigits+1 of good results (or fewer */
      /*                if early stop), starting at accnext+1 (its lsu) */
      /*       var1     has any residue at the stopping point */
      /*       accunits is the number of digits we collected in acc */
      if (accunits == 0)
	{			/* acc is 0 */
	  accunits = 1;		/* show we have one .. */
	  accdigits = 1;	/* .. */
	  *accnext = 0;		/* .. whose value is 0 */
	}
      else
	accnext++;		/* back to last placed */
      /* accnext now -> lowest unit of result */

      residue = 0;		/* assume no residue */
      if (op & DIVIDE)
	{
	  /* record the presence of any residue, for rounding */
	  if (*var1 != 0 || var1units > 1)
	    residue = 1;
	  else
	    {			/* no residue */
	      /* We had an exact division; clean up spurious trailing 0s. */
	      /* There will be at most DECDPUN-1, from the final multiply, */
	      /* and then only if the result is non-0 (and even) and the */
	      /* exponent is 'loose'. */
#if DECDPUN>1
	      Unit lsu = *accnext;
	      if (!(lsu & 0x01) && (lsu != 0))
		{
		  /* count the trailing zeros */
		  Int drop = 0;
		  for (;; drop++)
		    {		/* [will terminate because lsu!=0] */
		      if (exponent >= maxexponent)
			break;	/* don't chop real 0s */
#if DECDPUN<=4
		      if ((lsu - QUOT10 (lsu, drop + 1)
			   * powers[drop + 1]) != 0)
			break;	/* found non-0 digit */
#else
		      if (lsu % powers[drop + 1] != 0)
			break;	/* found non-0 digit */
#endif
		      exponent++;
		    }
		  if (drop > 0)
		    {
		      accunits = decShiftToLeast (accnext, accunits, drop);
		      accdigits = decGetDigits (accnext, accunits);
		      accunits = D2U (accdigits);
		      /* [exponent was adjusted in the loop] */
		    }
		}		/* neither odd nor 0 */
#endif
	    }			/* exact divide */
	}			/* divide */
      else			/* op!=DIVIDE */
	{
	  /* check for coefficient overflow */
	  if (accdigits + exponent > reqdigits)
	    {
	      *status |= DEC_Division_impossible;
	      break;
	    }
	  if (op & (REMAINDER | REMNEAR))
	    {
	      /* [Here, the exponent will be 0, because we adjusted var1 */
	      /* appropriately.] */
	      Int postshift;	/* work */
	      Flag wasodd = 0;	/* integer was odd */
	      Unit *quotlsu;	/* for save */
	      Int quotdigits;	/* .. */

	      /* Fastpath when residue is truly 0 is worthwhile [and */
	      /* simplifies the code below] */
	      if (*var1 == 0 && var1units == 1)
		{		/* residue is 0 */
		  Int exp = lhs->exponent;	/* save min(exponents) */
		  if (rhs->exponent < exp)
		    exp = rhs->exponent;
		  decNumberZero (res);	/* 0 coefficient */
#if DECSUBSET
		  if (set->extended)
#endif
		    res->exponent = exp;	/* .. with proper exponent */
		  break;
		}
	      /* note if the quotient was odd */
	      if (*accnext & 0x01)
		wasodd = 1;	/* acc is odd */
	      quotlsu = accnext;	/* save in case need to reinspect */
	      quotdigits = accdigits;	/* .. */

	      /* treat the residue, in var1, as the value to return, via acc */
	      /* calculate the unused zero digits.  This is the smaller of: */
	      /*   var1 initial padding (saved above) */
	      /*   var2 residual padding, which happens to be given by: */
	      postshift =
		var1initpad + exponent - lhs->exponent + rhs->exponent;
	      /* [the 'exponent' term accounts for the shifts during divide] */
	      if (var1initpad < postshift)
		postshift = var1initpad;

	      /* shift var1 the requested amount, and adjust its digits */
	      var1units = decShiftToLeast (var1, var1units, postshift);
	      accnext = var1;
	      accdigits = decGetDigits (var1, var1units);
	      accunits = D2U (accdigits);

	      exponent = lhs->exponent;	/* exponent is smaller of lhs & rhs */
	      if (rhs->exponent < exponent)
		exponent = rhs->exponent;
	      bits = lhs->bits;	/* remainder sign is always as lhs */

	      /* Now correct the result if we are doing remainderNear; if it */
	      /* (looking just at coefficients) is > rhs/2, or == rhs/2 and */
	      /* the integer was odd then the result should be rem-rhs. */
	      if (op & REMNEAR)
		{
		  Int compare, tarunits;	/* work */
		  Unit *up;	/* .. */


		  /* calculate remainder*2 into the var1 buffer (which has */
		  /* 'headroom' of an extra unit and hence enough space) */
		  /* [a dedicated 'double' loop would be faster, here] */
		  tarunits =
		    decUnitAddSub (accnext, accunits, accnext, accunits, 0,
				   accnext, 1);
		  /* decDumpAr('r', accnext, tarunits); */

		  /* Here, accnext (var1) holds tarunits Units with twice the */
		  /* remainder's coefficient, which we must now compare to the */
		  /* RHS.  The remainder's exponent may be smaller than the RHS's. */
		  compare =
		    decUnitCompare (accnext, tarunits, rhs->lsu,
				    D2U (rhs->digits),
				    rhs->exponent - exponent);
		  if (compare == BADINT)
		    {		/* deep trouble */
		      *status |= DEC_Insufficient_storage;
		      break;
		    }

		  /* now restore the remainder by dividing by two; we know the */
		  /* lsu is even. */
		  for (up = accnext; up < accnext + tarunits; up++)
		    {
		      Int half;	/* half to add to lower unit */
		      half = *up & 0x01;
		      *up /= 2;	/* [shift] */
		      if (!half)
			continue;
		      *(up - 1) += (DECDPUNMAX + 1) / 2;
		    }
		  /* [accunits still describes the original remainder length] */

		  if (compare > 0 || (compare == 0 && wasodd))
		    {		/* adjustment needed */
		      Int exp, expunits, exprem;	/* work */
		      /* This is effectively causing round-up of the quotient, */
		      /* so if it was the rare case where it was full and all */
		      /* nines, it would overflow and hence division-impossible */
		      /* should be raised */
		      Flag allnines = 0;	/* 1 if quotient all nines */
		      if (quotdigits == reqdigits)
			{	/* could be borderline */
			  for (up = quotlsu;; up++)
			    {
			      if (quotdigits > DECDPUN)
				{
				  if (*up != DECDPUNMAX)
				    break;	/* non-nines */
				}
			      else
				{	/* this is the last Unit */
				  if (*up == powers[quotdigits] - 1)
				    allnines = 1;
				  break;
				}
			      quotdigits -= DECDPUN;	/* checked those digits */
			    }	/* up */
			}	/* borderline check */
		      if (allnines)
			{
			  *status |= DEC_Division_impossible;
			  break;
			}

		      /* we need rem-rhs; the sign will invert.  Again we can */
		      /* safely use var1 for the working Units array. */
		      exp = rhs->exponent - exponent;	/* RHS padding needed */
		      /* Calculate units and remainder from exponent. */
		      expunits = exp / DECDPUN;
		      exprem = exp % DECDPUN;
		      /* subtract [A+B*(-m)]; the result will always be negative */
		      accunits = -decUnitAddSub (accnext, accunits,
						 rhs->lsu, D2U (rhs->digits),
						 expunits, accnext,
						 -(Int) powers[exprem]);
		      accdigits = decGetDigits (accnext, accunits);	/* count digits exactly */
		      accunits = D2U (accdigits);	/* and recalculate the units for copy */
		      /* [exponent is as for original remainder] */
		      bits ^= DECNEG;	/* flip the sign */
		    }
		}		/* REMNEAR */
	    }			/* REMAINDER or REMNEAR */
	}			/* not DIVIDE */

      /* Set exponent and bits */
      res->exponent = exponent;
      res->bits = (uByte) (bits & DECNEG);	/* [cleaned] */

      /* Now the coefficient. */
      decSetCoeff (res, set, accnext, accdigits, &residue, status);

      decFinish (res, set, &residue, status);	/* final cleanup */

#if DECSUBSET
      /* If a divide then strip trailing zeros if subset [after round] */
      if (!set->extended && (op == DIVIDE))
	decTrim (res, 0, &dropped);
#endif
    }
  while (0);			/* end protected */

  if (varalloc != NULL)
    free (varalloc);		/* drop any storage we used */
  if (allocacc != NULL)
    free (allocacc);		/* .. */
  if (allocrhs != NULL)
    free (allocrhs);		/* .. */
  if (alloclhs != NULL)
    free (alloclhs);		/* .. */
  return res;
}

/* ------------------------------------------------------------------ */
/* decMultiplyOp -- multiplication operation                          */
/*                                                                    */
/*  This routine performs the multiplication C=A x B.                 */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X*X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*   status is the usual accumulator                                  */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* ------------------------------------------------------------------ */
/* Note: We use 'long' multiplication rather than Karatsuba, as the   */
/* latter would give only a minor improvement for the short numbers   */
/* we expect to handle most (and uses much more memory).              */
/*                                                                    */
/* We always have to use a buffer for the accumulator.                */
/* ------------------------------------------------------------------ */
static decNumber *
decMultiplyOp (decNumber * res, const decNumber * lhs,
	       const decNumber * rhs, decContext * set, uInt * status)
{
  decNumber *alloclhs = NULL;	/* non-NULL if rounded lhs allocated */
  decNumber *allocrhs = NULL;	/* .., rhs */
  Unit accbuff[D2U (DECBUFFER * 2 + 1)];	/* local buffer (+1 in case DECBUFFER==0) */
  Unit *acc = accbuff;		/* -> accumulator array for exact result */
  Unit *allocacc = NULL;	/* -> allocated buffer, iff allocated */
  const Unit *mer, *mermsup;	/* work */
  Int accunits;			/* Units of accumulator in use */
  Int madlength;		/* Units in multiplicand */
  Int shift;			/* Units to shift multiplicand by */
  Int need;			/* Accumulator units needed */
  Int exponent;			/* work */
  Int residue = 0;		/* rounding residue */
  uByte bits;			/* result sign */
  uByte merged;			/* merged flags */

#if DECCHECK
  if (decCheckOperands (res, lhs, rhs, set))
    return res;
#endif

  do
    {				/* protect allocated storage */
#if DECSUBSET
      if (!set->extended)
	{
	  /* reduce operands and set lostDigits status, as needed */
	  if (lhs->digits > set->digits)
	    {
	      alloclhs = decRoundOperand (lhs, set, status);
	      if (alloclhs == NULL)
		break;
	      lhs = alloclhs;
	    }
	  if (rhs->digits > set->digits)
	    {
	      allocrhs = decRoundOperand (rhs, set, status);
	      if (allocrhs == NULL)
		break;
	      rhs = allocrhs;
	    }
	}
#endif
      /* [following code does not require input rounding] */

      /* precalculate result sign */
      bits = (uByte) ((lhs->bits ^ rhs->bits) & DECNEG);

      /* handle infinities and NaNs */
      merged = (lhs->bits | rhs->bits) & DECSPECIAL;
      if (merged)
	{			/* a special bit set */
	  if (merged & (DECSNAN | DECNAN))
	    {			/* one or two NaNs */
	      decNaNs (res, lhs, rhs, status);
	      break;
	    }
	  /* one or two infinities. Infinity * 0 is invalid */
	  if (((lhs->bits & DECSPECIAL) == 0 && ISZERO (lhs))
	      || ((rhs->bits & DECSPECIAL) == 0 && ISZERO (rhs)))
	    {
	      *status |= DEC_Invalid_operation;
	      break;
	    }
	  decNumberZero (res);
	  res->bits = bits | DECINF;	/* infinity */
	  break;
	}

      /* For best speed, as in DMSRCN, we use the shorter number as the */
      /* multiplier (rhs) and the longer as the multiplicand (lhs) */
      if (lhs->digits < rhs->digits)
	{			/* swap... */
	  const decNumber *hold = lhs;
	  lhs = rhs;
	  rhs = hold;
	}

      /* if accumulator is too long for local storage, then allocate */
      need = D2U (lhs->digits) + D2U (rhs->digits);	/* maximum units in result */
      if (need * sizeof (Unit) > sizeof (accbuff))
	{
	  allocacc = (Unit *) malloc (need * sizeof (Unit));
	  if (allocacc == NULL)
	    {
	      *status |= DEC_Insufficient_storage;
	      break;
	    }
	  acc = allocacc;	/* use the allocated space */
	}

      /* Now the main long multiplication loop */
      /* Unlike the equivalent in the IBM Java implementation, there */
      /* is no advantage in calculating from msu to lsu.  So we do it */
      /* by the book, as it were. */
      /* Each iteration calculates ACC=ACC+MULTAND*MULT */
      accunits = 1;		/* accumulator starts at '0' */
      *acc = 0;			/* .. (lsu=0) */
      shift = 0;		/* no multiplicand shift at first */
      madlength = D2U (lhs->digits);	/* we know this won't change */
      mermsup = rhs->lsu + D2U (rhs->digits);	/* -> msu+1 of multiplier */

      for (mer = rhs->lsu; mer < mermsup; mer++)
	{
	  /* Here, *mer is the next Unit in the multiplier to use */
	  /* If non-zero [optimization] add it... */
	  if (*mer != 0)
	    {
	      accunits =
		decUnitAddSub (&acc[shift], accunits - shift, lhs->lsu,
			       madlength, 0, &acc[shift], *mer) + shift;
	    }
	  else
	    {			/* extend acc with a 0; we'll use it shortly */
	      /* [this avoids length of <=0 later] */
	      *(acc + accunits) = 0;
	      accunits++;
	    }
	  /* multiply multiplicand by 10**DECDPUN for next Unit to left */
	  shift++;		/* add this for 'logical length' */
	}			/* n */
#if DECTRACE
      /* Show exact result */
      decDumpAr ('*', acc, accunits);
#endif

      /* acc now contains the exact result of the multiplication */
      /* Build a decNumber from it, noting if any residue */
      res->bits = bits;		/* set sign */
      res->digits = decGetDigits (acc, accunits);	/* count digits exactly */

      /* We might have a 31-bit wrap in calculating the exponent. */
      /* This can only happen if both input exponents are negative and */
      /* both their magnitudes are large.  If we did wrap, we set a safe */
      /* very negative exponent, from which decFinalize() will raise a */
      /* hard underflow. */
      exponent = lhs->exponent + rhs->exponent;	/* calculate exponent */
      if (lhs->exponent < 0 && rhs->exponent < 0 && exponent > 0)
	exponent = -2 * DECNUMMAXE;	/* force underflow */
      res->exponent = exponent;	/* OK to overwrite now */

      /* Set the coefficient.  If any rounding, residue records */
      decSetCoeff (res, set, acc, res->digits, &residue, status);

      decFinish (res, set, &residue, status);	/* final cleanup */
    }
  while (0);			/* end protected */

  if (allocacc != NULL)
    free (allocacc);		/* drop any storage we used */
  if (allocrhs != NULL)
    free (allocrhs);		/* .. */
  if (alloclhs != NULL)
    free (alloclhs);		/* .. */
  return res;
}

/* ------------------------------------------------------------------ */
/* decQuantizeOp  -- force exponent to requested value                */
/*                                                                    */
/*   This computes C = op(A, B), where op adjusts the coefficient     */
/*   of C (by rounding or shifting) such that the exponent (-scale)   */
/*   of C has the value B or matches the exponent of B.               */
/*   The numerical value of C will equal A, except for the effects of */
/*   any rounding that occurred.                                      */
/*                                                                    */
/*   res is C, the result.  C may be A or B                           */
/*   lhs is A, the number to adjust                                   */
/*   rhs is B, the requested exponent                                 */
/*   set is the context                                               */
/*   quant is 1 for quantize or 0 for rescale                         */
/*   status is the status accumulator (this can be called without     */
/*          risk of control loss)                                     */
/*                                                                    */
/* C must have space for set->digits digits.                          */
/*                                                                    */
/* Unless there is an error or the result is infinite, the exponent   */
/* after the operation is guaranteed to be that requested.            */
/* ------------------------------------------------------------------ */
static decNumber *
decQuantizeOp (decNumber * res, const decNumber * lhs,
	       const decNumber * rhs, decContext * set, Flag quant, uInt * status)
{
  decNumber *alloclhs = NULL;	/* non-NULL if rounded lhs allocated */
  decNumber *allocrhs = NULL;	/* .., rhs */
  const decNumber *inrhs = rhs;	/* save original rhs */
  Int reqdigits = set->digits;	/* requested DIGITS */
  Int reqexp;			/* requested exponent [-scale] */
  Int residue = 0;		/* rounding residue */
  uByte merged;			/* merged flags */
  Int etiny = set->emin - (set->digits - 1);

#if DECCHECK
  if (decCheckOperands (res, lhs, rhs, set))
    return res;
#endif

  do
    {				/* protect allocated storage */
#if DECSUBSET
      if (!set->extended)
	{
	  /* reduce operands and set lostDigits status, as needed */
	  if (lhs->digits > reqdigits)
	    {
	      alloclhs = decRoundOperand (lhs, set, status);
	      if (alloclhs == NULL)
		break;
	      lhs = alloclhs;
	    }
	  if (rhs->digits > reqdigits)
	    {			/* [this only checks lostDigits] */
	      allocrhs = decRoundOperand (rhs, set, status);
	      if (allocrhs == NULL)
		break;
	      rhs = allocrhs;
	    }
	}
#endif
      /* [following code does not require input rounding] */

      /* Handle special values */
      merged = (lhs->bits | rhs->bits) & DECSPECIAL;
      if ((lhs->bits | rhs->bits) & DECSPECIAL)
	{
	  /* NaNs get usual processing */
	  if (merged & (DECSNAN | DECNAN))
	    decNaNs (res, lhs, rhs, status);
	  /* one infinity but not both is bad */
	  else if ((lhs->bits ^ rhs->bits) & DECINF)
	    *status |= DEC_Invalid_operation;
	  /* both infinity: return lhs */
	  else
	    decNumberCopy (res, lhs);	/* [nop if in place] */
	  break;
	}

      /* set requested exponent */
      if (quant)
	reqexp = inrhs->exponent;	/* quantize -- match exponents */
      else
	{			/* rescale -- use value of rhs */
	  /* Original rhs must be an integer that fits and is in range */
#if DECSUBSET
	  reqexp = decGetInt (inrhs, set);
#else
	  reqexp = decGetInt (inrhs);
#endif
	}

#if DECSUBSET
      if (!set->extended)
	etiny = set->emin;	/* no subnormals */
#endif

      if (reqexp == BADINT	/* bad (rescale only) or .. */
	  || (reqexp < etiny)	/* < lowest */
	  || (reqexp > set->emax))
	{			/* > Emax */
	  *status |= DEC_Invalid_operation;
	  break;
	}

      /* we've processed the RHS, so we can overwrite it now if necessary */
      if (ISZERO (lhs))
	{			/* zero coefficient unchanged */
	  decNumberCopy (res, lhs);	/* [nop if in place] */
	  res->exponent = reqexp;	/* .. just set exponent */
#if DECSUBSET
	  if (!set->extended)
	    res->bits = 0;	/* subset specification; no -0 */
#endif
	}
      else
	{			/* non-zero lhs */
	  Int adjust = reqexp - lhs->exponent;	/* digit adjustment needed */
	  /* if adjusted coefficient will not fit, give up now */
	  if ((lhs->digits - adjust) > reqdigits)
	    {
	      *status |= DEC_Invalid_operation;
	      break;
	    }

	  if (adjust > 0)
	    {			/* increasing exponent */
	      /* this will decrease the length of the coefficient by adjust */
	      /* digits, and must round as it does so */
	      decContext workset;	/* work */
	      workset = *set;	/* clone rounding, etc. */
	      workset.digits = lhs->digits - adjust;	/* set requested length */
	      /* [note that the latter can be <1, here] */
	      decCopyFit (res, lhs, &workset, &residue, status);	/* fit to result */
	      decApplyRound (res, &workset, residue, status);	/* .. and round */
	      residue = 0;	/* [used] */
	      /* If we rounded a 999s case, exponent will be off by one; */
	      /* adjust back if so. */
	      if (res->exponent > reqexp)
		{
		  res->digits = decShiftToMost (res->lsu, res->digits, 1);	/* shift */
		  res->exponent--;	/* (re)adjust the exponent. */
		}
#if DECSUBSET
	      if (ISZERO (res) && !set->extended)
		res->bits = 0;	/* subset; no -0 */
#endif
	    }			/* increase */
	  else			/* adjust<=0 */
	    {			/* decreasing or = exponent */
	      /* this will increase the length of the coefficient by -adjust */
	      /* digits, by adding trailing zeros. */
	      decNumberCopy (res, lhs);	/* [it will fit] */
	      /* if padding needed (adjust<0), add it now... */
	      if (adjust < 0)
		{
		  res->digits =
		    decShiftToMost (res->lsu, res->digits, -adjust);
		  res->exponent += adjust;	/* adjust the exponent */
		}
	    }			/* decrease */
	}			/* non-zero */

      /* Check for overflow [do not use Finalize in this case, as an */
      /* overflow here is a "don't fit" situation] */
      if (res->exponent > set->emax - res->digits + 1)
	{			/* too big */
	  *status |= DEC_Invalid_operation;
	  break;
	}
      else
	{
	  decFinalize (res, set, &residue, status);	/* set subnormal flags */
	  *status &= ~DEC_Underflow;	/* suppress Underflow [754r] */
	}
    }
  while (0);			/* end protected */

  if (allocrhs != NULL)
    free (allocrhs);		/* drop any storage we used */
  if (alloclhs != NULL)
    free (alloclhs);		/* .. */
  return res;
}

/* ------------------------------------------------------------------ */
/* decCompareOp -- compare, min, or max two Numbers                   */
/*                                                                    */
/*   This computes C = A ? B and returns the signum (as a Number)     */
/*   for COMPARE or the maximum or minimum (for COMPMAX and COMPMIN). */
/*                                                                    */
/*   res is C, the result.  C may be A and/or B (e.g., X=X?X)         */
/*   lhs is A                                                         */
/*   rhs is B                                                         */
/*   set is the context                                               */
/*   op  is the operation flag                                        */
/*   status is the usual accumulator                                  */
/*                                                                    */
/* C must have space for one digit for COMPARE or set->digits for     */
/* COMPMAX and COMPMIN.                                               */
/* ------------------------------------------------------------------ */
/* The emphasis here is on speed for common cases, and avoiding       */
/* coefficient comparison if possible.                                */
/* ------------------------------------------------------------------ */
decNumber *
decCompareOp (decNumber * res, const decNumber * lhs, const decNumber * rhs,
	      decContext * set, Flag op, uInt * status)
{
  decNumber *alloclhs = NULL;	/* non-NULL if rounded lhs allocated */
  decNumber *allocrhs = NULL;	/* .., rhs */
  Int result = 0;		/* default result value */
  uByte merged;			/* merged flags */
  uByte bits = 0;		/* non-0 for NaN */

#if DECCHECK
  if (decCheckOperands (res, lhs, rhs, set))
    return res;
#endif

  do
    {				/* protect allocated storage */
#if DECSUBSET
      if (!set->extended)
	{
	  /* reduce operands and set lostDigits status, as needed */
	  if (lhs->digits > set->digits)
	    {
	      alloclhs = decRoundOperand (lhs, set, status);
	      if (alloclhs == NULL)
		{
		  result = BADINT;
		  break;
		}
	      lhs = alloclhs;
	    }
	  if (rhs->digits > set->digits)
	    {
	      allocrhs = decRoundOperand (rhs, set, status);
	      if (allocrhs == NULL)
		{
		  result = BADINT;
		  break;
		}
	      rhs = allocrhs;
	    }
	}
#endif
      /* [following code does not require input rounding] */

      /* handle NaNs now; let infinities drop through */
      /* +++ review sNaN handling with 754r, for now assumes sNaN */
      /* (even just one) leads to NaN. */
      merged = (lhs->bits | rhs->bits) & (DECSNAN | DECNAN);
      if (merged)
	{			/* a NaN bit set */
	  if (op == COMPARE);
	  else if (merged & DECSNAN);
	  else
	    {			/* 754r rules for MIN and MAX ignore single NaN */
	      /* here if MIN or MAX, and one or two quiet NaNs */
	      if (lhs->bits & rhs->bits & DECNAN);
	      else
		{		/* just one quiet NaN */
		  /* force choice to be the non-NaN operand */
		  op = COMPMAX;
		  if (lhs->bits & DECNAN)
		    result = -1;	/* pick rhs */
		  else
		    result = +1;	/* pick lhs */
		  break;
		}
	    }
	  op = COMPNAN;		/* use special path */
	  decNaNs (res, lhs, rhs, status);
	  break;
	}

      result = decCompare (lhs, rhs);	/* we have numbers */
    }
  while (0);			/* end protected */

  if (result == BADINT)
    *status |= DEC_Insufficient_storage;	/* rare */
  else
    {
      if (op == COMPARE)
	{			/* return signum */
	  decNumberZero (res);	/* [always a valid result] */
	  if (result == 0)
	    res->bits = bits;	/* (maybe qNaN) */
	  else
	    {
	      *res->lsu = 1;
	      if (result < 0)
		res->bits = DECNEG;
	    }
	}
      else if (op == COMPNAN);	/* special, drop through */
      else
	{			/* MAX or MIN, non-NaN result */
	  Int residue = 0;	/* rounding accumulator */
	  /* choose the operand for the result */
	  const decNumber *choice;
	  if (result == 0)
	    {			/* operands are numerically equal */
	      /* choose according to sign then exponent (see 754r) */
	      uByte slhs = (lhs->bits & DECNEG);
	      uByte srhs = (rhs->bits & DECNEG);
#if DECSUBSET
	      if (!set->extended)
		{		/* subset: force left-hand */
		  op = COMPMAX;
		  result = +1;
		}
	      else
#endif
	      if (slhs != srhs)
		{		/* signs differ */
		  if (slhs)
		    result = -1;	/* rhs is max */
		  else
		    result = +1;	/* lhs is max */
		}
	      else if (slhs && srhs)
		{		/* both negative */
		  if (lhs->exponent < rhs->exponent)
		    result = +1;
		  else
		    result = -1;
		  /* [if equal, we use lhs, technically identical] */
		}
	      else
		{		/* both positive */
		  if (lhs->exponent > rhs->exponent)
		    result = +1;
		  else
		    result = -1;
		  /* [ditto] */
		}
	    }			/* numerically equal */
	  /* here result will be non-0 */
	  if (op == COMPMIN)
	    result = -result;	/* reverse if looking for MIN */
	  choice = (result > 0 ? lhs : rhs);	/* choose */
	  /* copy chosen to result, rounding if need be */
	  decCopyFit (res, choice, set, &residue, status);
	  decFinish (res, set, &residue, status);
	}
    }
  if (allocrhs != NULL)
    free (allocrhs);		/* free any storage we used */
  if (alloclhs != NULL)
    free (alloclhs);		/* .. */
  return res;
}

/* ------------------------------------------------------------------ */
/* decCompare -- compare two decNumbers by numerical value            */
/*                                                                    */
/*  This routine compares A ? B without altering them.                */
/*                                                                    */
/*  Arg1 is A, a decNumber which is not a NaN                         */
/*  Arg2 is B, a decNumber which is not a NaN                         */
/*                                                                    */
/*  returns -1, 0, or 1 for A<B, A==B, or A>B, or BADINT if failure   */
/*  (the only possible failure is an allocation error)                */
/* ------------------------------------------------------------------ */
/* This could be merged into decCompareOp */
static Int
decCompare (const decNumber * lhs, const decNumber * rhs)
{
  Int result;			/* result value */
  Int sigr;			/* rhs signum */
  Int compare;			/* work */
  result = 1;			/* assume signum(lhs) */
  if (ISZERO (lhs))
    result = 0;
  else if (decNumberIsNegative (lhs))
    result = -1;
  sigr = 1;			/* compute signum(rhs) */
  if (ISZERO (rhs))
    sigr = 0;
  else if (decNumberIsNegative (rhs))
    sigr = -1;
  if (result > sigr)
    return +1;			/* L > R, return 1 */
  if (result < sigr)
    return -1;			/* R < L, return -1 */

  /* signums are the same */
  if (result == 0)
    return 0;			/* both 0 */
  /* Both non-zero */
  if ((lhs->bits | rhs->bits) & DECINF)
    {				/* one or more infinities */
      if (lhs->bits == rhs->bits)
	result = 0;		/* both the same */
      else if (decNumberIsInfinite (rhs))
	result = -result;
      return result;
    }

  /* we must compare the coefficients, allowing for exponents */
  if (lhs->exponent > rhs->exponent)
    {				/* LHS exponent larger */
      /* swap sides, and sign */
      const decNumber *temp = lhs;
      lhs = rhs;
      rhs = temp;
      result = -result;
    }

  compare = decUnitCompare (lhs->lsu, D2U (lhs->digits),
			    rhs->lsu, D2U (rhs->digits),
			    rhs->exponent - lhs->exponent);

  if (compare != BADINT)
    compare *= result;		/* comparison succeeded */
  return compare;		/* what we got */
}

/* ------------------------------------------------------------------ */
/* decUnitCompare -- compare two >=0 integers in Unit arrays          */
/*                                                                    */
/*  This routine compares A ? B*10**E where A and B are unit arrays   */
/*  A is a plain integer                                              */
/*  B has an exponent of E (which must be non-negative)               */
/*                                                                    */
/*  Arg1 is A first Unit (lsu)                                        */
/*  Arg2 is A length in Units                                         */
/*  Arg3 is B first Unit (lsu)                                        */
/*  Arg4 is B length in Units                                         */
/*  Arg5 is E                                                         */
/*                                                                    */
/*  returns -1, 0, or 1 for A<B, A==B, or A>B, or BADINT if failure   */
/*  (the only possible failure is an allocation error)                */
/* ------------------------------------------------------------------ */
static Int
decUnitCompare (const Unit * a, Int alength, const Unit * b, Int blength, Int exp)
{
  Unit *acc;			/* accumulator for result */
  Unit accbuff[D2U (DECBUFFER + 1)];	/* local buffer */
  Unit *allocacc = NULL;	/* -> allocated acc buffer, iff allocated */
  Int accunits, need;		/* units in use or needed for acc */
  const Unit *l, *r, *u;	/* work */
  Int expunits, exprem, result;	/* .. */

  if (exp == 0)
    {				/* aligned; fastpath */
      if (alength > blength)
	return 1;
      if (alength < blength)
	return -1;
      /* same number of units in both -- need unit-by-unit compare */
      l = a + alength - 1;
      r = b + alength - 1;
      for (; l >= a; l--, r--)
	{
	  if (*l > *r)
	    return 1;
	  if (*l < *r)
	    return -1;
	}
      return 0;			/* all units match */
    }				/* aligned */

  /* Unaligned.  If one is >1 unit longer than the other, padded */
  /* approximately, then we can return easily */
  if (alength > blength + (Int) D2U (exp))
    return 1;
  if (alength + 1 < blength + (Int) D2U (exp))
    return -1;

  /* We need to do a real subtract.  For this, we need a result buffer */
  /* even though we only are interested in the sign.  Its length needs */
  /* to be the larger of alength and padded blength, +2 */
  need = blength + D2U (exp);	/* maximum real length of B */
  if (need < alength)
    need = alength;
  need += 2;
  acc = accbuff;		/* assume use local buffer */
  if (need * sizeof (Unit) > sizeof (accbuff))
    {
      allocacc = (Unit *) malloc (need * sizeof (Unit));
      if (allocacc == NULL)
	return BADINT;		/* hopeless -- abandon */
      acc = allocacc;
    }
  /* Calculate units and remainder from exponent. */
  expunits = exp / DECDPUN;
  exprem = exp % DECDPUN;
  /* subtract [A+B*(-m)] */
  accunits = decUnitAddSub (a, alength, b, blength, expunits, acc,
			    -(Int) powers[exprem]);
  /* [UnitAddSub result may have leading zeros, even on zero] */
  if (accunits < 0)
    result = -1;		/* negative result */
  else
    {				/* non-negative result */
      /* check units of the result before freeing any storage */
      for (u = acc; u < acc + accunits - 1 && *u == 0;)
	u++;
      result = (*u == 0 ? 0 : +1);
    }
  /* clean up and return the result */
  if (allocacc != NULL)
    free (allocacc);		/* drop any storage we used */
  return result;
}

/* ------------------------------------------------------------------ */
/* decUnitAddSub -- add or subtract two >=0 integers in Unit arrays   */
/*                                                                    */
/*  This routine performs the calculation:                            */
/*                                                                    */
/*  C=A+(B*M)                                                         */
/*                                                                    */
/*  Where M is in the range -DECDPUNMAX through +DECDPUNMAX.          */
/*                                                                    */
/*  A may be shorter or longer than B.                                */
/*                                                                    */
/*  Leading zeros are not removed after a calculation.  The result is */
/*  either the same length as the longer of A and B (adding any       */
/*  shift), or one Unit longer than that (if a Unit carry occurred).  */
/*                                                                    */
/*  A and B content are not altered unless C is also A or B.          */
/*  C may be the same array as A or B, but only if no zero padding is */
/*  requested (that is, C may be B only if bshift==0).                */
/*  C is filled from the lsu; only those units necessary to complete  */
/*  the calculation are referenced.                                   */
/*                                                                    */
/*  Arg1 is A first Unit (lsu)                                        */
/*  Arg2 is A length in Units                                         */
/*  Arg3 is B first Unit (lsu)                                        */
/*  Arg4 is B length in Units                                         */
/*  Arg5 is B shift in Units  (>=0; pads with 0 units if positive)    */
/*  Arg6 is C first Unit (lsu)                                        */
/*  Arg7 is M, the multiplier                                         */
/*                                                                    */
/*  returns the count of Units written to C, which will be non-zero   */
/*  and negated if the result is negative.  That is, the sign of the  */
/*  returned Int is the sign of the result (positive for zero) and    */
/*  the absolute value of the Int is the count of Units.              */
/*                                                                    */
/*  It is the caller's responsibility to make sure that C size is     */
/*  safe, allowing space if necessary for a one-Unit carry.           */
/*                                                                    */
/*  This routine is severely performance-critical; *any* change here  */
/*  must be measured (timed) to assure no performance degradation.    */
/*  In particular, trickery here tends to be counter-productive, as   */
/*  increased complexity of code hurts register optimizations on      */
/*  register-poor architectures.  Avoiding divisions is nearly        */
/*  always a Good Idea, however.                                      */
/*                                                                    */
/* Special thanks to Rick McGuire (IBM Cambridge, MA) and Dave Clark  */
/* (IBM Warwick, UK) for some of the ideas used in this routine.      */
/* ------------------------------------------------------------------ */
static Int
decUnitAddSub (const Unit * a, Int alength,
	       const Unit * b, Int blength, Int bshift, Unit * c, Int m)
{
  const Unit *alsu = a;		/* A lsu [need to remember it] */
  Unit *clsu = c;		/* C ditto */
  Unit *minC;			/* low water mark for C */
  Unit *maxC;			/* high water mark for C */
  eInt carry = 0;		/* carry integer (could be Long) */
  Int add;			/* work */
#if DECDPUN==4			/* myriadal */
  Int est;			/* estimated quotient */
#endif

#if DECTRACE
  if (alength < 1 || blength < 1)
    printf ("decUnitAddSub: alen blen m %d %d [%d]\n", alength, blength, m);
#endif

  maxC = c + alength;		/* A is usually the longer */
  minC = c + blength;		/* .. and B the shorter */
  if (bshift != 0)
    {				/* B is shifted; low As copy across */
      minC += bshift;
      /* if in place [common], skip copy unless there's a gap [rare] */
      if (a == c && bshift <= alength)
	{
	  c += bshift;
	  a += bshift;
	}
      else
	for (; c < clsu + bshift; a++, c++)
	  {			/* copy needed */
	    if (a < alsu + alength)
	      *c = *a;
	    else
	      *c = 0;
	  }
    }
  if (minC > maxC)
    {				/* swap */
      Unit *hold = minC;
      minC = maxC;
      maxC = hold;
    }

  /* For speed, we do the addition as two loops; the first where both A */
  /* and B contribute, and the second (if necessary) where only one or */
  /* other of the numbers contribute. */
  /* Carry handling is the same (i.e., duplicated) in each case. */
  for (; c < minC; c++)
    {
      carry += *a;
      a++;
      carry += ((eInt) * b) * m;	/* [special-casing m=1/-1 */
      b++;			/* here is not a win] */
      /* here carry is new Unit of digits; it could be +ve or -ve */
      if ((ueInt) carry <= DECDPUNMAX)
	{			/* fastpath 0-DECDPUNMAX */
	  *c = (Unit) carry;
	  carry = 0;
	  continue;
	}
      /* remainder operator is undefined if negative, so we must test */
#if DECDPUN==4			/* use divide-by-multiply */
      if (carry >= 0)
	{
	  est = (((ueInt) carry >> 11) * 53687) >> 18;
	  *c = (Unit) (carry - est * (DECDPUNMAX + 1));	/* remainder */
	  carry = est;		/* likely quotient [89%] */
	  if (*c < DECDPUNMAX + 1)
	    continue;		/* estimate was correct */
	  carry++;
	  *c -= DECDPUNMAX + 1;
	  continue;
	}
      /* negative case */
      carry = carry + (eInt) (DECDPUNMAX + 1) * (DECDPUNMAX + 1);	/* make positive */
      est = (((ueInt) carry >> 11) * 53687) >> 18;
      *c = (Unit) (carry - est * (DECDPUNMAX + 1));
      carry = est - (DECDPUNMAX + 1);	/* correctly negative */
      if (*c < DECDPUNMAX + 1)
	continue;		/* was OK */
      carry++;
      *c -= DECDPUNMAX + 1;
#else
      if ((ueInt) carry < (DECDPUNMAX + 1) * 2)
	{			/* fastpath carry +1 */
	  *c = (Unit) (carry - (DECDPUNMAX + 1));	/* [helps additions] */
	  carry = 1;
	  continue;
	}
      if (carry >= 0)
	{
	  *c = (Unit) (carry % (DECDPUNMAX + 1));
	  carry = carry / (DECDPUNMAX + 1);
	  continue;
	}
      /* negative case */
      carry = carry + (eInt) (DECDPUNMAX + 1) * (DECDPUNMAX + 1);	/* make positive */
      *c = (Unit) (carry % (DECDPUNMAX + 1));
      carry = carry / (DECDPUNMAX + 1) - (DECDPUNMAX + 1);
#endif
    }				/* c */

  /* we now may have one or other to complete */
  /* [pretest to avoid loop setup/shutdown] */
  if (c < maxC)
    for (; c < maxC; c++)
      {
	if (a < alsu + alength)
	  {			/* still in A */
	    carry += *a;
	    a++;
	  }
	else
	  {			/* inside B */
	    carry += ((eInt) * b) * m;
	    b++;
	  }
	/* here carry is new Unit of digits; it could be +ve or -ve and */
	/* magnitude up to DECDPUNMAX squared */
	if ((ueInt) carry <= DECDPUNMAX)
	  {			/* fastpath 0-DECDPUNMAX */
	    *c = (Unit) carry;
	    carry = 0;
	    continue;
	  }
	/* result for this unit is negative or >DECDPUNMAX */
#if DECDPUN==4			/* use divide-by-multiply */
	/* remainder is undefined if negative, so we must test */
	if (carry >= 0)
	  {
	    est = (((ueInt) carry >> 11) * 53687) >> 18;
	    *c = (Unit) (carry - est * (DECDPUNMAX + 1));	/* remainder */
	    carry = est;	/* likely quotient [79.7%] */
	    if (*c < DECDPUNMAX + 1)
	      continue;		/* estimate was correct */
	    carry++;
	    *c -= DECDPUNMAX + 1;
	    continue;
	  }
	/* negative case */
	carry = carry + (eInt) (DECDPUNMAX + 1) * (DECDPUNMAX + 1);	/* make positive */
	est = (((ueInt) carry >> 11) * 53687) >> 18;
	*c = (Unit) (carry - est * (DECDPUNMAX + 1));
	carry = est - (DECDPUNMAX + 1);	/* correctly negative */
	if (*c < DECDPUNMAX + 1)
	  continue;		/* was OK */
	carry++;
	*c -= DECDPUNMAX + 1;
#else
	if ((ueInt) carry < (DECDPUNMAX + 1) * 2)
	  {			/* fastpath carry 1 */
	    *c = (Unit) (carry - (DECDPUNMAX + 1));
	    carry = 1;
	    continue;
	  }
	/* remainder is undefined if negative, so we must test */
	if (carry >= 0)
	  {
	    *c = (Unit) (carry % (DECDPUNMAX + 1));
	    carry = carry / (DECDPUNMAX + 1);
	    continue;
	  }
	/* negative case */
	carry = carry + (eInt) (DECDPUNMAX + 1) * (DECDPUNMAX + 1);	/* make positive */
	*c = (Unit) (carry % (DECDPUNMAX + 1));
	carry = carry / (DECDPUNMAX + 1) - (DECDPUNMAX + 1);
#endif
      }				/* c */

  /* OK, all A and B processed; might still have carry or borrow */
  /* return number of Units in the result, negated if a borrow */
  if (carry == 0)
    return c - clsu;		/* no carry, we're done */
  if (carry > 0)
    {				/* positive carry */
      *c = (Unit) carry;	/* place as new unit */
      c++;			/* .. */
      return c - clsu;
    }
  /* -ve carry: it's a borrow; complement needed */
  add = 1;			/* temporary carry... */
  for (c = clsu; c < maxC; c++)
    {
      add = DECDPUNMAX + add - *c;
      if (add <= DECDPUNMAX)
	{
	  *c = (Unit) add;
	  add = 0;
	}
      else
	{
	  *c = 0;
	  add = 1;
	}
    }
  /* add an extra unit iff it would be non-zero */
#if DECTRACE
  printf ("UAS borrow: add %d, carry %d\n", add, carry);
#endif
  if ((add - carry - 1) != 0)
    {
      *c = (Unit) (add - carry - 1);
      c++;			/* interesting, include it */
    }
  return clsu - c;		/* -ve result indicates borrowed */
}

/* ------------------------------------------------------------------ */
/* decTrim -- trim trailing zeros or normalize                        */
/*                                                                    */
/*   dn is the number to trim or normalize                            */
/*   all is 1 to remove all trailing zeros, 0 for just fraction ones  */
/*   dropped returns the number of discarded trailing zeros           */
/*   returns dn                                                       */
/*                                                                    */
/* All fields are updated as required.  This is a utility operation,  */
/* so special values are unchanged and no error is possible.          */
/* ------------------------------------------------------------------ */
static decNumber *
decTrim (decNumber * dn, Flag all, Int * dropped)
{
  Int d, exp;			/* work */
  uInt cut;			/* .. */
  Unit *up;			/* -> current Unit */

#if DECCHECK
  if (decCheckOperands (dn, DECUNUSED, DECUNUSED, DECUNUSED))
    return dn;
#endif

  *dropped = 0;			/* assume no zeros dropped */
  if ((dn->bits & DECSPECIAL)	/* fast exit if special .. */
      || (*dn->lsu & 0x01))
    return dn;			/* .. or odd */
  if (ISZERO (dn))
    {				/* .. or 0 */
      dn->exponent = 0;		/* (sign is preserved) */
      return dn;
    }

  /* we have a finite number which is even */
  exp = dn->exponent;
  cut = 1;			/* digit (1-DECDPUN) in Unit */
  up = dn->lsu;			/* -> current Unit */
  for (d = 0; d < dn->digits - 1; d++)
    {				/* [don't strip the final digit] */
      /* slice by powers */
#if DECDPUN<=4
      uInt quot = QUOT10 (*up, cut);
      if ((*up - quot * powers[cut]) != 0)
	break;			/* found non-0 digit */
#else
      if (*up % powers[cut] != 0)
	break;			/* found non-0 digit */
#endif
      /* have a trailing 0 */
      if (!all)
	{			/* trimming */
	  /* [if exp>0 then all trailing 0s are significant for trim] */
	  if (exp <= 0)
	    {			/* if digit might be significant */
	      if (exp == 0)
		break;		/* then quit */
	      exp++;		/* next digit might be significant */
	    }
	}
      cut++;			/* next power */
      if (cut > DECDPUN)
	{			/* need new Unit */
	  up++;
	  cut = 1;
	}
    }				/* d */
  if (d == 0)
    return dn;			/* none dropped */

  /* effect the drop */
  decShiftToLeast (dn->lsu, D2U (dn->digits), d);
  dn->exponent += d;		/* maintain numerical value */
  dn->digits -= d;		/* new length */
  *dropped = d;			/* report the count */
  return dn;
}

/* ------------------------------------------------------------------ */
/* decShiftToMost -- shift digits in array towards most significant   */
/*                                                                    */
/*   uar    is the array                                              */
/*   digits is the count of digits in use in the array                */
/*   shift  is the number of zeros to pad with (least significant);   */
/*     it must be zero or positive                                    */
/*                                                                    */
/*   returns the new length of the integer in the array, in digits    */
/*                                                                    */
/* No overflow is permitted (that is, the uar array must be known to  */
/* be large enough to hold the result, after shifting).               */
/* ------------------------------------------------------------------ */
static Int
decShiftToMost (Unit * uar, Int digits, Int shift)
{
  Unit *target, *source, *first;	/* work */
  uInt rem;			/* for division */
  Int cut;			/* odd 0's to add */
  uInt next;			/* work */

  if (shift == 0)
    return digits;		/* [fastpath] nothing to do */
  if ((digits + shift) <= DECDPUN)
    {				/* [fastpath] single-unit case */
      *uar = (Unit) (*uar * powers[shift]);
      return digits + shift;
    }

  cut = (DECDPUN - shift % DECDPUN) % DECDPUN;
  source = uar + D2U (digits) - 1;	/* where msu comes from */
  first = uar + D2U (digits + shift) - 1;	/* where msu of source will end up */
  target = source + D2U (shift);	/* where upper part of first cut goes */
  next = 0;

  for (; source >= uar; source--, target--)
    {
      /* split the source Unit and accumulate remainder for next */
#if DECDPUN<=4
      uInt quot = QUOT10 (*source, cut);
      rem = *source - quot * powers[cut];
      next += quot;
#else
      rem = *source % powers[cut];
      next += *source / powers[cut];
#endif
      if (target <= first)
	*target = (Unit) next;	/* write to target iff valid */
      next = rem * powers[DECDPUN - cut];	/* save remainder for next Unit */
    }
  /* propagate to one below and clear the rest */
  for (; target >= uar; target--)
    {
      *target = (Unit) next;
      next = 0;
    }
  return digits + shift;
}

/* ------------------------------------------------------------------ */
/* decShiftToLeast -- shift digits in array towards least significant */
/*                                                                    */
/*   uar   is the array                                               */
/*   units is length of the array, in units                           */
/*   shift is the number of digits to remove from the lsu end; it     */
/*     must be zero or positive and less than units*DECDPUN.          */
/*                                                                    */
/*   returns the new length of the integer in the array, in units     */
/*                                                                    */
/* Removed digits are discarded (lost).  Units not required to hold   */
/* the final result are unchanged.                                    */
/* ------------------------------------------------------------------ */
static Int
decShiftToLeast (Unit * uar, Int units, Int shift)
{
  Unit *target, *up;		/* work */
  Int cut, count;		/* work */
  Int quot, rem;		/* for division */

  if (shift == 0)
    return units;		/* [fastpath] nothing to do */

  up = uar + shift / DECDPUN;	/* source; allow for whole Units */
  cut = shift % DECDPUN;	/* odd 0's to drop */
  target = uar;			/* both paths */
  if (cut == 0)
    {				/* whole units shift */
      for (; up < uar + units; target++, up++)
	*target = *up;
      return target - uar;
    }
  /* messier */
  count = units * DECDPUN - shift;	/* the maximum new length */
#if DECDPUN<=4
  quot = QUOT10 (*up, cut);
#else
  quot = *up / powers[cut];
#endif
  for (;; target++)
    {
      *target = (Unit) quot;
      count -= (DECDPUN - cut);
      if (count <= 0)
	break;
      up++;
      quot = *up;
#if DECDPUN<=4
      quot = QUOT10 (quot, cut);
      rem = *up - quot * powers[cut];
#else
      rem = quot % powers[cut];
      quot = quot / powers[cut];
#endif
      *target = (Unit) (*target + rem * powers[DECDPUN - cut]);
      count -= cut;
      if (count <= 0)
	break;
    }
  return target - uar + 1;
}

#if DECSUBSET
/* ------------------------------------------------------------------ */
/* decRoundOperand -- round an operand  [used for subset only]        */
/*                                                                    */
/*   dn is the number to round (dn->digits is > set->digits)          */
/*   set is the relevant context                                      */
/*   status is the status accumulator                                 */
/*                                                                    */
/*   returns an allocated decNumber with the rounded result.          */
/*                                                                    */
/* lostDigits and other status may be set by this.                    */
/*                                                                    */
/* Since the input is an operand, we are not permitted to modify it.  */
/* We therefore return an allocated decNumber, rounded as required.   */
/* It is the caller's responsibility to free the allocated storage.   */
/*                                                                    */
/* If no storage is available then the result cannot be used, so NULL */
/* is returned.                                                       */
/* ------------------------------------------------------------------ */
static decNumber *
decRoundOperand (const decNumber * dn, decContext * set, uInt * status)
{
  decNumber *res;		/* result structure */
  uInt newstatus = 0;		/* status from round */
  Int residue = 0;		/* rounding accumulator */

  /* Allocate storage for the returned decNumber, big enough for the */
  /* length specified by the context */
  res = (decNumber *) malloc (sizeof (decNumber)
			      + (D2U (set->digits) - 1) * sizeof (Unit));
  if (res == NULL)
    {
      *status |= DEC_Insufficient_storage;
      return NULL;
    }
  decCopyFit (res, dn, set, &residue, &newstatus);
  decApplyRound (res, set, residue, &newstatus);

  /* If that set Inexact then we "lost digits" */
  if (newstatus & DEC_Inexact)
    newstatus |= DEC_Lost_digits;
  *status |= newstatus;
  return res;
}
#endif

/* ------------------------------------------------------------------ */
/* decCopyFit -- copy a number, shortening the coefficient if needed  */
/*                                                                    */
/*   dest is the target decNumber                                     */
/*   src  is the source decNumber                                     */
/*   set is the context [used for length (digits) and rounding mode]  */
/*   residue is the residue accumulator                               */
/*   status contains the current status to be updated                 */
/*                                                                    */
/* (dest==src is allowed and will be a no-op if fits)                 */
/* All fields are updated as required.                                */
/* ------------------------------------------------------------------ */
static void
decCopyFit (decNumber * dest, const decNumber * src, decContext * set,
	    Int * residue, uInt * status)
{
  dest->bits = src->bits;
  dest->exponent = src->exponent;
  decSetCoeff (dest, set, src->lsu, src->digits, residue, status);
}

/* ------------------------------------------------------------------ */
/* decSetCoeff -- set the coefficient of a number                     */
/*                                                                    */
/*   dn    is the number whose coefficient array is to be set.        */
/*         It must have space for set->digits digits                  */
/*   set   is the context [for size]                                  */
/*   lsu   -> lsu of the source coefficient [may be dn->lsu]          */
/*   len   is digits in the source coefficient [may be dn->digits]    */
/*   residue is the residue accumulator.  This has values as in       */
/*         decApplyRound, and will be unchanged unless the            */
/*         target size is less than len.  In this case, the           */
/*         coefficient is truncated and the residue is updated to     */
/*         reflect the previous residue and the dropped digits.       */
/*   status is the status accumulator, as usual                       */
/*                                                                    */
/* The coefficient may already be in the number, or it can be an      */
/* external intermediate array.  If it is in the number, lsu must ==  */
/* dn->lsu and len must == dn->digits.                                */
/*                                                                    */
/* Note that the coefficient length (len) may be < set->digits, and   */
/* in this case this merely copies the coefficient (or is a no-op     */
/* if dn->lsu==lsu).                                                  */
/*                                                                    */
/* Note also that (only internally, from decNumberRescale and         */
/* decSetSubnormal) the value of set->digits may be less than one,    */
/* indicating a round to left.                                        */
/* This routine handles that case correctly; caller ensures space.    */
/*                                                                    */
/* dn->digits, dn->lsu (and as required), and dn->exponent are        */
/* updated as necessary.   dn->bits (sign) is unchanged.              */
/*                                                                    */
/* DEC_Rounded status is set if any digits are discarded.             */
/* DEC_Inexact status is set if any non-zero digits are discarded, or */
/*                       incoming residue was non-0 (implies rounded) */
/* ------------------------------------------------------------------ */
/* mapping array: maps 0-9 to canonical residues, so that we can */
/* adjust by a residue in range [-1, +1] and achieve correct rounding */
/*                             0  1  2  3  4  5  6  7  8  9 */
static const uByte resmap[10] = { 0, 3, 3, 3, 3, 5, 7, 7, 7, 7 };
static void
decSetCoeff (decNumber * dn, decContext * set, const Unit * lsu,
	     Int len, Int * residue, uInt * status)
{
  Int discard;			/* number of digits to discard */
  uInt discard1;		/* first discarded digit */
  uInt cut;			/* cut point in Unit */
  uInt quot, rem;		/* for divisions */
  Unit *target;			/* work */
  const Unit *up;		/* work */
  Int count;			/* .. */
#if DECDPUN<=4
  uInt temp;			/* .. */
#endif

  discard = len - set->digits;	/* digits to discard */
  if (discard <= 0)
    {				/* no digits are being discarded */
      if (dn->lsu != lsu)
	{			/* copy needed */
	  /* copy the coefficient array to the result number; no shift needed */
	  up = lsu;
	  for (target = dn->lsu; target < dn->lsu + D2U (len); target++, up++)
	    {
	      *target = *up;
	    }
	  dn->digits = len;	/* set the new length */
	}
      /* dn->exponent and residue are unchanged */
      if (*residue != 0)
	*status |= (DEC_Inexact | DEC_Rounded);	/* record inexactitude */
      return;
    }

  /* we have to discard some digits */
  *status |= DEC_Rounded;	/* accumulate Rounded status */
  if (*residue > 1)
    *residue = 1;		/* previous residue now to right, so -1 to +1 */

  if (discard > len)
    {				/* everything, +1, is being discarded */
      /* guard digit is 0 */
      /* residue is all the number [NB could be all 0s] */
      if (*residue <= 0)
	for (up = lsu + D2U (len) - 1; up >= lsu; up--)
	  {
	    if (*up != 0)
	      {			/* found a non-0 */
		*residue = 1;
		break;		/* no need to check any others */
	      }
	  }
      if (*residue != 0)
	*status |= DEC_Inexact;	/* record inexactitude */
      *dn->lsu = 0;		/* coefficient will now be 0 */
      dn->digits = 1;		/* .. */
      dn->exponent += discard;	/* maintain numerical value */
      return;
    }				/* total discard */

  /* partial discard [most common case] */
  /* here, at least the first (most significant) discarded digit exists */

  /* spin up the number, noting residue as we pass, until we get to */
  /* the Unit with the first discarded digit.  When we get there, */
  /* extract it and remember where we're at */
  count = 0;
  for (up = lsu;; up++)
    {
      count += DECDPUN;
      if (count >= discard)
	break;			/* full ones all checked */
      if (*up != 0)
	*residue = 1;
    }				/* up */

  /* here up -> Unit with discarded digit */
  cut = discard - (count - DECDPUN) - 1;
  if (cut == DECDPUN - 1)
    {				/* discard digit is at top */
#if DECDPUN<=4
      discard1 = QUOT10 (*up, DECDPUN - 1);
      rem = *up - discard1 * powers[DECDPUN - 1];
#else
      rem = *up % powers[DECDPUN - 1];
      discard1 = *up / powers[DECDPUN - 1];
#endif
      if (rem != 0)
	*residue = 1;
      up++;			/* move to next */
      cut = 0;			/* bottom digit of result */
      quot = 0;			/* keep a certain compiler happy */
    }
  else
    {
      /* discard digit is in low digit(s), not top digit */
      if (cut == 0)
	quot = *up;
      else			/* cut>0 */
	{			/* it's not at bottom of Unit */
#if DECDPUN<=4
	  quot = QUOT10 (*up, cut);
	  rem = *up - quot * powers[cut];
#else
	  rem = *up % powers[cut];
	  quot = *up / powers[cut];
#endif
	  if (rem != 0)
	    *residue = 1;
	}
      /* discard digit is now at bottom of quot */
#if DECDPUN<=4
      temp = (quot * 6554) >> 16;	/* fast /10 */
      /* Vowels algorithm here not a win (9 instructions) */
      discard1 = quot - X10 (temp);
      quot = temp;
#else
      discard1 = quot % 10;
      quot = quot / 10;
#endif
      cut++;			/* update cut */
    }

  /* here: up -> Unit of the array with discarded digit */
  /*       cut is the division point for each Unit */
  /*       quot holds the uncut high-order digits for the current */
  /*            Unit, unless cut==0 in which case it's still in *up */
  /* copy the coefficient array to the result number, shifting as we go */
  count = set->digits;		/* digits to end up with */
  if (count <= 0)
    {				/* special for Rescale/Subnormal :-( */
      *dn->lsu = 0;		/* .. result is 0 */
      dn->digits = 1;		/* .. */
    }
  else
    {				/* shift to least */
      /* [this is similar to decShiftToLeast code, with copy] */
      dn->digits = count;	/* set the new length */
      if (cut == 0)
	{
	  /* on unit boundary, so simple shift down copy loop suffices */
	  for (target = dn->lsu; target < dn->lsu + D2U (count);
	       target++, up++)
	    {
	      *target = *up;
	    }
	}
      else
	for (target = dn->lsu;; target++)
	  {
	    *target = (Unit) quot;
	    count -= (DECDPUN - cut);
	    if (count <= 0)
	      break;
	    up++;
	    quot = *up;
#if DECDPUN<=4
	    quot = QUOT10 (quot, cut);
	    rem = *up - quot * powers[cut];
#else
	    rem = quot % powers[cut];
	    quot = quot / powers[cut];
#endif
	    *target = (Unit) (*target + rem * powers[DECDPUN - cut]);
	    count -= cut;
	    if (count <= 0)
	      break;
	  }
    }				/* shift to least needed */
  dn->exponent += discard;	/* maintain numerical value */

  /* here, discard1 is the guard digit, and residue is everything else */
  /* [use mapping to accumulate residue safely] */
  *residue += resmap[discard1];

  if (*residue != 0)
    *status |= DEC_Inexact;	/* record inexactitude */
  return;
}

/* ------------------------------------------------------------------ */
/* decApplyRound -- apply pending rounding to a number                */
/*                                                                    */
/*   dn    is the number, with space for set->digits digits           */
/*   set   is the context [for size and rounding mode]                */
/*   residue indicates pending rounding, being any accumulated        */
/*         guard and sticky information.  It may be:                  */
/*         6-9: rounding digit is >5                                  */
/*         5:   rounding digit is exactly half-way                    */
/*         1-4: rounding digit is <5 and >0                           */
/*         0:   the coefficient is exact                              */
/*        -1:   as 1, but the hidden digits are subtractive, that     */
/*              is, of the opposite sign to dn.  In this case the     */
/*              coefficient must be non-0.                            */
/*   status is the status accumulator, as usual                       */
/*                                                                    */
/* This routine applies rounding while keeping the length of the      */
/* coefficient constant.  The exponent and status are unchanged       */
/* except if:                                                         */
/*                                                                    */
/*   -- the coefficient was increased and is all nines (in which      */
/*      case Overflow could occur, and is handled directly here so    */
/*      the caller does not need to re-test for overflow)             */
/*                                                                    */
/*   -- the coefficient was decreased and becomes all nines (in which */
/*      case Underflow could occur, and is also handled directly).    */
/*                                                                    */
/* All fields in dn are updated as required.                          */
/*                                                                    */
/* ------------------------------------------------------------------ */
static void
decApplyRound (decNumber * dn, decContext * set, Int residue, uInt * status)
{
  Int bump;			/* 1 if coefficient needs to be incremented */
  /* -1 if coefficient needs to be decremented */

  if (residue == 0)
    return;			/* nothing to apply */

  bump = 0;			/* assume a smooth ride */

  /* now decide whether, and how, to round, depending on mode */
  switch (set->round)
    {
    case DEC_ROUND_DOWN:
      {
	/* no change, except if negative residue */
	if (residue < 0)
	  bump = -1;
	break;
      }				/* r-d */

    case DEC_ROUND_HALF_DOWN:
      {
	if (residue > 5)
	  bump = 1;
	break;
      }				/* r-h-d */

    case DEC_ROUND_HALF_EVEN:
      {
	if (residue > 5)
	  bump = 1;		/* >0.5 goes up */
	else if (residue == 5)
	  {			/* exactly 0.5000... */
	    /* 0.5 goes up iff [new] lsd is odd */
	    if (*dn->lsu & 0x01)
	      bump = 1;
	  }
	break;
      }				/* r-h-e */

    case DEC_ROUND_HALF_UP:
      {
	if (residue >= 5)
	  bump = 1;
	break;
      }				/* r-h-u */

    case DEC_ROUND_UP:
      {
	if (residue > 0)
	  bump = 1;
	break;
      }				/* r-u */

    case DEC_ROUND_CEILING:
      {
	/* same as _UP for positive numbers, and as _DOWN for negatives */
	/* [negative residue cannot occur on 0] */
	if (decNumberIsNegative (dn))
	  {
	    if (residue < 0)
	      bump = -1;
	  }
	else
	  {
	    if (residue > 0)
	      bump = 1;
	  }
	break;
      }				/* r-c */

    case DEC_ROUND_FLOOR:
      {
	/* same as _UP for negative numbers, and as _DOWN for positive */
	/* [negative residue cannot occur on 0] */
	if (!decNumberIsNegative (dn))
	  {
	    if (residue < 0)
	      bump = -1;
	  }
	else
	  {
	    if (residue > 0)
	      bump = 1;
	  }
	break;
      }				/* r-f */

    default:
      {				/* e.g., DEC_ROUND_MAX */
	*status |= DEC_Invalid_context;
#if DECTRACE
	printf ("Unknown rounding mode: %d\n", set->round);
#endif
	break;
      }
    }				/* switch */

  /* now bump the number, up or down, if need be */
  if (bump == 0)
    return;			/* no action required */

  /* Simply use decUnitAddSub unless we are bumping up and the number */
  /* is all nines.  In this special case we set to 1000... and adjust */
  /* the exponent by one (as otherwise we could overflow the array) */
  /* Similarly handle all-nines result if bumping down. */
  if (bump > 0)
    {
      Unit *up;			/* work */
      uInt count = dn->digits;	/* digits to be checked */
      for (up = dn->lsu;; up++)
	{
	  if (count <= DECDPUN)
	    {
	      /* this is the last Unit (the msu) */
	      if (*up != powers[count] - 1)
		break;		/* not still 9s */
	      /* here if it, too, is all nines */
	      *up = (Unit) powers[count - 1];	/* here 999 -> 100 etc. */
	      for (up = up - 1; up >= dn->lsu; up--)
		*up = 0;	/* others all to 0 */
	      dn->exponent++;	/* and bump exponent */
	      /* [which, very rarely, could cause Overflow...] */
	      if ((dn->exponent + dn->digits) > set->emax + 1)
		{
		  decSetOverflow (dn, set, status);
		}
	      return;		/* done */
	    }
	  /* a full unit to check, with more to come */
	  if (*up != DECDPUNMAX)
	    break;		/* not still 9s */
	  count -= DECDPUN;
	}			/* up */
    }				/* bump>0 */
  else
    {				/* -1 */
      /* here we are lookng for a pre-bump of 1000... (leading 1, */
      /* all other digits zero) */
      Unit *up, *sup;		/* work */
      uInt count = dn->digits;	/* digits to be checked */
      for (up = dn->lsu;; up++)
	{
	  if (count <= DECDPUN)
	    {
	      /* this is the last Unit (the msu) */
	      if (*up != powers[count - 1])
		break;		/* not 100.. */
	      /* here if we have the 1000... case */
	      sup = up;		/* save msu pointer */
	      *up = (Unit) powers[count] - 1;	/* here 100 in msu -> 999 */
	      /* others all to all-nines, too */
	      for (up = up - 1; up >= dn->lsu; up--)
		*up = (Unit) powers[DECDPUN] - 1;
	      dn->exponent--;	/* and bump exponent */

	      /* iff the number was at the subnormal boundary (exponent=etiny) */
	      /* then the exponent is now out of range, so it will in fact get */
	      /* clamped to etiny and the final 9 dropped. */
	      /* printf(">> emin=%d exp=%d sdig=%d\n", set->emin, */
	      /*        dn->exponent, set->digits); */
	      if (dn->exponent + 1 == set->emin - set->digits + 1)
		{
		  if (count == 1 && dn->digits == 1)
		    *sup = 0;	/* here 9 -> 0[.9] */
		  else
		    {
		      *sup = (Unit) powers[count - 1] - 1;	/* here 999.. in msu -> 99.. */
		      dn->digits--;
		    }
		  dn->exponent++;
		  *status |=
		    DEC_Underflow | DEC_Subnormal | DEC_Inexact | DEC_Rounded;
		}
	      return;		/* done */
	    }

	  /* a full unit to check, with more to come */
	  if (*up != 0)
	    break;		/* not still 0s */
	  count -= DECDPUN;
	}			/* up */

    }				/* bump<0 */

  /* Actual bump needed.  Do it. */
  decUnitAddSub (dn->lsu, D2U (dn->digits), one, 1, 0, dn->lsu, bump);
}

#if DECSUBSET
/* ------------------------------------------------------------------ */
/* decFinish -- finish processing a number                            */
/*                                                                    */
/*   dn is the number                                                 */
/*   set is the context                                               */
/*   residue is the rounding accumulator (as in decApplyRound)        */
/*   status is the accumulator                                        */
/*                                                                    */
/* This finishes off the current number by:                           */
/*    1. If not extended:                                             */
/*       a. Converting a zero result to clean '0'                     */
/*       b. Reducing positive exponents to 0, if would fit in digits  */
/*    2. Checking for overflow and subnormals (always)                */
/* Note this is just Finalize when no subset arithmetic.              */
/* All fields are updated as required.                                */
/* ------------------------------------------------------------------ */
static void
decFinish (decNumber * dn, decContext * set, Int * residue, uInt * status)
{
  if (!set->extended)
    {
      if ISZERO
	(dn)
	{			/* value is zero */
	  dn->exponent = 0;	/* clean exponent .. */
	  dn->bits = 0;		/* .. and sign */
	  return;		/* no error possible */
	}
      if (dn->exponent >= 0)
	{			/* non-negative exponent */
	  /* >0; reduce to integer if possible */
	  if (set->digits >= (dn->exponent + dn->digits))
	    {
	      dn->digits = decShiftToMost (dn->lsu, dn->digits, dn->exponent);
	      dn->exponent = 0;
	    }
	}
    }				/* !extended */

  decFinalize (dn, set, residue, status);
}
#endif

/* ------------------------------------------------------------------ */
/* decFinalize -- final check, clamp, and round of a number           */
/*                                                                    */
/*   dn is the number                                                 */
/*   set is the context                                               */
/*   residue is the rounding accumulator (as in decApplyRound)        */
/*   status is the status accumulator                                 */
/*                                                                    */
/* This finishes off the current number by checking for subnormal     */
/* results, applying any pending rounding, checking for overflow,     */
/* and applying any clamping.                                         */
/* Underflow and overflow conditions are raised as appropriate.       */
/* All fields are updated as required.                                */
/* ------------------------------------------------------------------ */
static void
decFinalize (decNumber * dn, decContext * set, Int * residue, uInt * status)
{
  Int shift;			/* shift needed if clamping */

  /* We have to be careful when checking the exponent as the adjusted */
  /* exponent could overflow 31 bits [because it may already be up */
  /* to twice the expected]. */

  /* First test for subnormal.  This must be done before any final */
  /* round as the result could be rounded to Nmin or 0. */
  if (dn->exponent < 0		/* negative exponent */
      && (dn->exponent < set->emin - dn->digits + 1))
    {
      /* Go handle subnormals; this will apply round if needed. */
      decSetSubnormal (dn, set, residue, status);
      return;
    }

  /* now apply any pending round (this could raise overflow). */
  if (*residue != 0)
    decApplyRound (dn, set, *residue, status);

  /* Check for overflow [redundant in the 'rare' case] or clamp */
  if (dn->exponent <= set->emax - set->digits + 1)
    return;			/* neither needed */

  /* here when we might have an overflow or clamp to do */
  if (dn->exponent > set->emax - dn->digits + 1)
    {				/* too big */
      decSetOverflow (dn, set, status);
      return;
    }
  /* here when the result is normal but in clamp range */
  if (!set->clamp)
    return;

  /* here when we need to apply the IEEE exponent clamp (fold-down) */
  shift = dn->exponent - (set->emax - set->digits + 1);

  /* shift coefficient (if non-zero) */
  if (!ISZERO (dn))
    {
      dn->digits = decShiftToMost (dn->lsu, dn->digits, shift);
    }
  dn->exponent -= shift;	/* adjust the exponent to match */
  *status |= DEC_Clamped;	/* and record the dirty deed */
  return;
}

/* ------------------------------------------------------------------ */
/* decSetOverflow -- set number to proper overflow value              */
/*                                                                    */
/*   dn is the number (used for sign [only] and result)               */
/*   set is the context [used for the rounding mode]                  */
/*   status contains the current status to be updated                 */
/*                                                                    */
/* This sets the sign of a number and sets its value to either        */
/* Infinity or the maximum finite value, depending on the sign of     */
/* dn and therounding mode, following IEEE 854 rules.                 */
/* ------------------------------------------------------------------ */
static void
decSetOverflow (decNumber * dn, decContext * set, uInt * status)
{
  Flag needmax = 0;		/* result is maximum finite value */
  uByte sign = dn->bits & DECNEG;	/* clean and save sign bit */

  if (ISZERO (dn))
    {				/* zero does not overflow magnitude */
      Int emax = set->emax;	/* limit value */
      if (set->clamp)
	emax -= set->digits - 1;	/* lower if clamping */
      if (dn->exponent > emax)
	{			/* clamp required */
	  dn->exponent = emax;
	  *status |= DEC_Clamped;
	}
      return;
    }

  decNumberZero (dn);
  switch (set->round)
    {
    case DEC_ROUND_DOWN:
      {
	needmax = 1;		/* never Infinity */
	break;
      }				/* r-d */
    case DEC_ROUND_CEILING:
      {
	if (sign)
	  needmax = 1;		/* Infinity if non-negative */
	break;
      }				/* r-c */
    case DEC_ROUND_FLOOR:
      {
	if (!sign)
	  needmax = 1;		/* Infinity if negative */
	break;
      }				/* r-f */
    default:
      break;			/* Infinity in all other cases */
    }
  if (needmax)
    {
      Unit *up;			/* work */
      Int count = set->digits;	/* nines to add */
      dn->digits = count;
      /* fill in all nines to set maximum value */
      for (up = dn->lsu;; up++)
	{
	  if (count > DECDPUN)
	    *up = DECDPUNMAX;	/* unit full o'nines */
	  else
	    {			/* this is the msu */
	      *up = (Unit) (powers[count] - 1);
	      break;
	    }
	  count -= DECDPUN;	/* we filled those digits */
	}			/* up */
      dn->bits = sign;		/* sign */
      dn->exponent = set->emax - set->digits + 1;
    }
  else
    dn->bits = sign | DECINF;	/* Value is +/-Infinity */
  *status |= DEC_Overflow | DEC_Inexact | DEC_Rounded;
}

/* ------------------------------------------------------------------ */
/* decSetSubnormal -- process value whose exponent is <Emin           */
/*                                                                    */
/*   dn is the number (used as input as well as output; it may have   */
/*         an allowed subnormal value, which may need to be rounded)  */
/*   set is the context [used for the rounding mode]                  */
/*   residue is any pending residue                                   */
/*   status contains the current status to be updated                 */
/*                                                                    */
/* If subset mode, set result to zero and set Underflow flags.        */
/*                                                                    */
/* Value may be zero with a low exponent; this does not set Subnormal */
/* but the exponent will be clamped to Etiny.                         */
/*                                                                    */
/* Otherwise ensure exponent is not out of range, and round as        */
/* necessary.  Underflow is set if the result is Inexact.             */
/* ------------------------------------------------------------------ */
static void
decSetSubnormal (decNumber * dn, decContext * set,
		 Int * residue, uInt * status)
{
  decContext workset;		/* work */
  Int etiny, adjust;		/* .. */

#if DECSUBSET
  /* simple set to zero and 'hard underflow' for subset */
  if (!set->extended)
    {
      decNumberZero (dn);
      /* always full overflow */
      *status |= DEC_Underflow | DEC_Subnormal | DEC_Inexact | DEC_Rounded;
      return;
    }
#endif

  /* Full arithmetic -- allow subnormals, rounded to minimum exponent */
  /* (Etiny) if needed */
  etiny = set->emin - (set->digits - 1);	/* smallest allowed exponent */

  if ISZERO
    (dn)
    {				/* value is zero */
      /* residue can never be non-zero here */
#if DECCHECK
      if (*residue != 0)
	{
	  printf ("++ Subnormal 0 residue %d\n", *residue);
	  *status |= DEC_Invalid_operation;
	}
#endif
      if (dn->exponent < etiny)
	{			/* clamp required */
	  dn->exponent = etiny;
	  *status |= DEC_Clamped;
	}
      return;
    }

  *status |= DEC_Subnormal;	/* we have a non-zero subnormal */

  adjust = etiny - dn->exponent;	/* calculate digits to remove */
  if (adjust <= 0)
    {				/* not out of range; unrounded */
      /* residue can never be non-zero here, so fast-path out */
#if DECCHECK
      if (*residue != 0)
	{
	  printf ("++ Subnormal no-adjust residue %d\n", *residue);
	  *status |= DEC_Invalid_operation;
	}
#endif
      /* it may already be inexact (from setting the coefficient) */
      if (*status & DEC_Inexact)
	*status |= DEC_Underflow;
      return;
    }

  /* adjust>0.  we need to rescale the result so exponent becomes Etiny */
  /* [this code is similar to that in rescale] */
  workset = *set;		/* clone rounding, etc. */
  workset.digits = dn->digits - adjust;	/* set requested length */
  workset.emin -= adjust;	/* and adjust emin to match */
  /* [note that the latter can be <1, here, similar to Rescale case] */
  decSetCoeff (dn, &workset, dn->lsu, dn->digits, residue, status);
  decApplyRound (dn, &workset, *residue, status);

  /* Use 754R/854 default rule: Underflow is set iff Inexact */
  /* [independent of whether trapped] */
  if (*status & DEC_Inexact)
    *status |= DEC_Underflow;

  /* if we rounded up a 999s case, exponent will be off by one; adjust */
  /* back if so [it will fit, because we shortened] */
  if (dn->exponent > etiny)
    {
      dn->digits = decShiftToMost (dn->lsu, dn->digits, 1);
      dn->exponent--;		/* (re)adjust the exponent. */
    }
}

/* ------------------------------------------------------------------ */
/* decGetInt -- get integer from a number                             */
/*                                                                    */
/*   dn is the number [which will not be altered]                     */
/*   set is the context [requested digits], subset only               */
/*   returns the converted integer, or BADINT if error                */
/*                                                                    */
/* This checks and gets a whole number from the input decNumber.      */
/* The magnitude of the integer must be <2^31.                        */
/* Any discarded fractional part must be 0.                           */
/* If subset it must also fit in set->digits                          */
/* ------------------------------------------------------------------ */
#if DECSUBSET
static Int
decGetInt (const decNumber * dn, decContext * set)
{
#else
static Int
decGetInt (const decNumber * dn)
{
#endif
  Int theInt;			/* result accumulator */
  const Unit *up;		/* work */
  Int got;			/* digits (real or not) processed */
  Int ilength = dn->digits + dn->exponent;	/* integral length */

  /* The number must be an integer that fits in 10 digits */
  /* Assert, here, that 10 is enough for any rescale Etiny */
#if DEC_MAX_EMAX > 999999999
#error GetInt may need updating [for Emax]
#endif
#if DEC_MIN_EMIN < -999999999
#error GetInt may need updating [for Emin]
#endif
  if (ISZERO (dn))
    return 0;			/* zeros are OK, with any exponent */
  if (ilength > 10)
    return BADINT;		/* always too big */
#if DECSUBSET
  if (!set->extended && ilength > set->digits)
    return BADINT;
#endif

  up = dn->lsu;			/* ready for lsu */
  theInt = 0;			/* ready to accumulate */
  if (dn->exponent >= 0)
    {				/* relatively easy */
      /* no fractional part [usual]; allow for positive exponent */
      got = dn->exponent;
    }
  else
    {				/* -ve exponent; some fractional part to check and discard */
      Int count = -dn->exponent;	/* digits to discard */
      /* spin up whole units until we get to the Unit with the unit digit */
      for (; count >= DECDPUN; up++)
	{
	  if (*up != 0)
	    return BADINT;	/* non-zero Unit to discard */
	  count -= DECDPUN;
	}
      if (count == 0)
	got = 0;		/* [a multiple of DECDPUN] */
      else
	{			/* [not multiple of DECDPUN] */
	  Int rem;		/* work */
	  /* slice off fraction digits and check for non-zero */
#if DECDPUN<=4
	  theInt = QUOT10 (*up, count);
	  rem = *up - theInt * powers[count];
#else
	  rem = *up % powers[count];	/* slice off discards */
	  theInt = *up / powers[count];
#endif
	  if (rem != 0)
	    return BADINT;	/* non-zero fraction */
	  /* OK, we're good */
	  got = DECDPUN - count;	/* number of digits so far */
	  up++;			/* ready for next */
	}
    }
  /* collect the rest */
  for (; got < ilength; up++)
    {
      theInt += *up * powers[got];
      got += DECDPUN;
    }
  if ((ilength == 10)		/* check no wrap */
      && (theInt / (Int) powers[got - DECDPUN] != *(up - 1)))
    return BADINT;
  /* [that test also disallows the BADINT result case] */

  /* apply any sign and return */
  if (decNumberIsNegative (dn))
    theInt = -theInt;
  return theInt;
}

/* ------------------------------------------------------------------ */
/* decStrEq -- caseless comparison of strings                         */
/*                                                                    */
/*   str1 is one of the strings to compare                            */
/*   str2 is the other                                                */
/*                                                                    */
/*   returns 1 if strings caseless-compare equal, 0 otherwise         */
/*                                                                    */
/* Note that the strings must be the same length if they are to       */
/* compare equal; there is no padding.                                */
/* ------------------------------------------------------------------ */
/* [strcmpi is not in ANSI C] */
static Flag
decStrEq (const char *str1, const char *str2)
{
  for (;; str1++, str2++)
    {
      unsigned char u1 = (unsigned char) *str1;
      unsigned char u2 = (unsigned char) *str2;
      if (u1 == u2)
	{
	  if (u1 == '\0')
	    break;
	}
      else
	{
	  if (tolower (u1) != tolower (u2))
	    return 0;
	}
    }				/* stepping */
  return 1;
}

/* ------------------------------------------------------------------ */
/* decNaNs -- handle NaN operand or operands                          */
/*                                                                    */
/*   res    is the result number                                      */
/*   lhs    is the first operand                                      */
/*   rhs    is the second operand, or NULL if none                    */
/*   status contains the current status                               */
/*   returns res in case convenient                                   */
/*                                                                    */
/* Called when one or both operands is a NaN, and propagates the      */
/* appropriate result to res.  When an sNaN is found, it is changed   */
/* to a qNaN and Invalid operation is set.                            */
/* ------------------------------------------------------------------ */
static decNumber *
decNaNs (decNumber * res, const decNumber * lhs, const decNumber * rhs, uInt * status)
{
  /* This decision tree ends up with LHS being the source pointer, */
  /* and status updated if need be */
  if (lhs->bits & DECSNAN)
    *status |= DEC_Invalid_operation | DEC_sNaN;
  else if (rhs == NULL);
  else if (rhs->bits & DECSNAN)
    {
      lhs = rhs;
      *status |= DEC_Invalid_operation | DEC_sNaN;
    }
  else if (lhs->bits & DECNAN);
  else
    lhs = rhs;

  decNumberCopy (res, lhs);
  res->bits &= ~DECSNAN;	/* convert any sNaN to NaN, while */
  res->bits |= DECNAN;		/* .. preserving sign */
  res->exponent = 0;		/* clean exponent */
  /* [coefficient was copied] */
  return res;
}

/* ------------------------------------------------------------------ */
/* decStatus -- apply non-zero status                                 */
/*                                                                    */
/*   dn     is the number to set if error                             */
/*   status contains the current status (not yet in context)          */
/*   set    is the context                                            */
/*                                                                    */
/* If the status is an error status, the number is set to a NaN,      */
/* unless the error was an overflow, divide-by-zero, or underflow,    */
/* in which case the number will have already been set.               */
/*                                                                    */
/* The context status is then updated with the new status.  Note that */
/* this may raise a signal, so control may never return from this     */
/* routine (hence resources must be recovered before it is called).   */
/* ------------------------------------------------------------------ */
static void
decStatus (decNumber * dn, uInt status, decContext * set)
{
  if (status & DEC_NaNs)
    {				/* error status -> NaN */
      /* if cause was an sNaN, clear and propagate [NaN is already set up] */
      if (status & DEC_sNaN)
	status &= ~DEC_sNaN;
      else
	{
	  decNumberZero (dn);	/* other error: clean throughout */
	  dn->bits = DECNAN;	/* and make a quiet NaN */
	}
    }
  decContextSetStatus (set, status);
  return;
}

/* ------------------------------------------------------------------ */
/* decGetDigits -- count digits in a Units array                      */
/*                                                                    */
/*   uar is the Unit array holding the number [this is often an       */
/*          accumulator of some sort]                                 */
/*   len is the length of the array in units                          */
/*                                                                    */
/*   returns the number of (significant) digits in the array          */
/*                                                                    */
/* All leading zeros are excluded, except the last if the array has   */
/* only zero Units.                                                   */
/* ------------------------------------------------------------------ */
/* This may be called twice during some operations. */
static Int
decGetDigits (const Unit * uar, Int len)
{
  const Unit *up = uar + len - 1;	/* -> msu */
  Int digits = len * DECDPUN;	/* maximum possible digits */
  uInt const *pow;		/* work */

  for (; up >= uar; up--)
    {
      digits -= DECDPUN;
      if (*up == 0)
	{			/* unit is 0 */
	  if (digits != 0)
	    continue;		/* more to check */
	  /* all units were 0 */
	  digits++;		/* .. so bump digits to 1 */
	  break;
	}
      /* found the first non-zero Unit */
      digits++;
      if (*up < 10)
	break;			/* fastpath 1-9 */
      digits++;
      for (pow = &powers[2]; *up >= *pow; pow++)
	digits++;
      break;
    }				/* up */

  return digits;
}


#if DECTRACE | DECCHECK
/* ------------------------------------------------------------------ */
/* decNumberShow -- display a number [debug aid]                      */
/*   dn is the number to show                                         */
/*                                                                    */
/* Shows: sign, exponent, coefficient (msu first), digits             */
/*    or: sign, special-value                                         */
/* ------------------------------------------------------------------ */
/* this is public so other modules can use it */
void
decNumberShow (const decNumber * dn)
{
  const Unit *up;		/* work */
  uInt u, d;			/* .. */
  Int cut;			/* .. */
  char isign = '+';		/* main sign */
  if (dn == NULL)
    {
      printf ("NULL\n");
      return;
    }
  if (decNumberIsNegative (dn))
    isign = '-';
  printf (" >> %c ", isign);
  if (dn->bits & DECSPECIAL)
    {				/* Is a special value */
      if (decNumberIsInfinite (dn))
	printf ("Infinity");
      else
	{			/* a NaN */
	  if (dn->bits & DECSNAN)
	    printf ("sNaN");	/* signalling NaN */
	  else
	    printf ("NaN");
	}
      /* if coefficient and exponent are 0, we're done */
      if (dn->exponent == 0 && dn->digits == 1 && *dn->lsu == 0)
	{
	  printf ("\n");
	  return;
	}
      /* drop through to report other information */
      printf (" ");
    }

  /* now carefully display the coefficient */
  up = dn->lsu + D2U (dn->digits) - 1;	/* msu */
  printf ("%d", *up);
  for (up = up - 1; up >= dn->lsu; up--)
    {
      u = *up;
      printf (":");
      for (cut = DECDPUN - 1; cut >= 0; cut--)
	{
	  d = u / powers[cut];
	  u -= d * powers[cut];
	  printf ("%d", d);
	}			/* cut */
    }				/* up */
  if (dn->exponent != 0)
    {
      char esign = '+';
      if (dn->exponent < 0)
	esign = '-';
      printf (" E%c%d", esign, abs (dn->exponent));
    }
  printf (" [%d]\n", dn->digits);
}
#endif

#if DECTRACE || DECCHECK
/* ------------------------------------------------------------------ */
/* decDumpAr -- display a unit array [debug aid]                      */
/*   name is a single-character tag name                              */
/*   ar   is the array to display                                     */
/*   len  is the length of the array in Units                         */
/* ------------------------------------------------------------------ */
static void
decDumpAr (char name, const Unit * ar, Int len)
{
  Int i;
#if DECDPUN==4
  const char *spec = "%04d ";
#else
  const char *spec = "%d ";
#endif
  printf ("  :%c: ", name);
  for (i = len - 1; i >= 0; i--)
    {
      if (i == len - 1)
	printf ("%d ", ar[i]);
      else
	printf (spec, ar[i]);
    }
  printf ("\n");
  return;
}
#endif

#if DECCHECK
/* ------------------------------------------------------------------ */
/* decCheckOperands -- check operand(s) to a routine                  */
/*   res is the result structure (not checked; it will be set to      */
/*          quiet NaN if error found (and it is not NULL))            */
/*   lhs is the first operand (may be DECUNUSED)                      */
/*   rhs is the second (may be DECUNUSED)                             */
/*   set is the context (may be DECUNUSED)                            */
/*   returns 0 if both operands, and the context are clean, or 1      */
/*     otherwise (in which case the context will show an error,       */
/*     unless NULL).  Note that res is not cleaned; caller should     */
/*     handle this so res=NULL case is safe.                          */
/* The caller is expected to abandon immediately if 1 is returned.    */
/* ------------------------------------------------------------------ */
static Flag
decCheckOperands (decNumber * res, const decNumber * lhs,
		  const decNumber * rhs, decContext * set)
{
  Flag bad = 0;
  if (set == NULL)
    {				/* oops; hopeless */
#if DECTRACE
      printf ("Context is NULL.\n");
#endif
      bad = 1;
      return 1;
    }
  else if (set != DECUNUSED
	   && (set->digits < 1 || set->round < 0
	       || set->round >= DEC_ROUND_MAX))
    {
      bad = 1;
#if DECTRACE
      printf ("Bad context [digits=%d round=%d].\n", set->digits, set->round);
#endif
    }
  else
    {
      if (res == NULL)
	{
	  bad = 1;
#if DECTRACE
	  printf ("Bad result [is NULL].\n");
#endif
	}
      if (!bad && lhs != DECUNUSED)
	bad = (decCheckNumber (lhs, set));
      if (!bad && rhs != DECUNUSED)
	bad = (decCheckNumber (rhs, set));
    }
  if (bad)
    {
      if (set != DECUNUSED)
	decContextSetStatus (set, DEC_Invalid_operation);
      if (res != DECUNUSED && res != NULL)
	{
	  decNumberZero (res);
	  res->bits = DECNAN;	/* qNaN */
	}
    }
  return bad;
}

/* ------------------------------------------------------------------ */
/* decCheckNumber -- check a number                                   */
/*   dn is the number to check                                        */
/*   set is the context (may be DECUNUSED)                            */
/*   returns 0 if the number is clean, or 1 otherwise                 */
/*                                                                    */
/* The number is considered valid if it could be a result from some   */
/* operation in some valid context (not necessarily the current one). */
/* ------------------------------------------------------------------ */
Flag
decCheckNumber (const decNumber * dn, decContext * set)
{
  const Unit *up;		/* work */
  uInt maxuint;			/* .. */
  Int ae, d, digits;		/* .. */
  Int emin, emax;		/* .. */

  if (dn == NULL)
    {				/* hopeless */
#if DECTRACE
      printf ("Reference to decNumber is NULL.\n");
#endif
      return 1;
    }

  /* check special values */
  if (dn->bits & DECSPECIAL)
    {
      if (dn->exponent != 0)
	{
#if DECTRACE
	  printf ("Exponent %d (not 0) for a special value.\n", dn->exponent);
#endif
	  return 1;
	}

      /* 2003.09.08: NaNs may now have coefficients, so next tests Inf only */
      if (decNumberIsInfinite (dn))
	{
	  if (dn->digits != 1)
	    {
#if DECTRACE
	      printf ("Digits %d (not 1) for an infinity.\n", dn->digits);
#endif
	      return 1;
	    }
	  if (*dn->lsu != 0)
	    {
#if DECTRACE
	      printf ("LSU %d (not 0) for an infinity.\n", *dn->lsu);
#endif
	      return 1;
	    }
	}			/* Inf */
      /* 2002.12.26: negative NaNs can now appear through proposed IEEE */
      /*             concrete formats (decimal64, etc.), though they are */
      /*             never visible in strings. */
      return 0;

      /* if ((dn->bits & DECINF) || (dn->bits & DECNEG)==0) return 0; */
      /* #if DECTRACE */
      /* printf("Negative NaN in number.\n"); */
      /* #endif */
      /* return 1; */
    }

  /* check the coefficient */
  if (dn->digits < 1 || dn->digits > DECNUMMAXP)
    {
#if DECTRACE
      printf ("Digits %d in number.\n", dn->digits);
#endif
      return 1;
    }

  d = dn->digits;

  for (up = dn->lsu; d > 0; up++)
    {
      if (d > DECDPUN)
	maxuint = DECDPUNMAX;
      else
	{			/* we are at the msu */
	  maxuint = powers[d] - 1;
	  if (dn->digits > 1 && *up < powers[d - 1])
	    {
#if DECTRACE
	      printf ("Leading 0 in number.\n");
	      decNumberShow (dn);
#endif
	      return 1;
	    }
	}
      if (*up > maxuint)
	{
#if DECTRACE
	  printf ("Bad Unit [%08x] in number at offset %d [maxuint %d].\n",
		  *up, up - dn->lsu, maxuint);
#endif
	  return 1;
	}
      d -= DECDPUN;
    }

  /* check the exponent.  Note that input operands can have exponents */
  /* which are out of the set->emin/set->emax and set->digits range */
  /* (just as they can have more digits than set->digits). */
  ae = dn->exponent + dn->digits - 1;	/* adjusted exponent */
  emax = DECNUMMAXE;
  emin = DECNUMMINE;
  digits = DECNUMMAXP;
  if (ae < emin - (digits - 1))
    {
#if DECTRACE
      printf ("Adjusted exponent underflow [%d].\n", ae);
      decNumberShow (dn);
#endif
      return 1;
    }
  if (ae > +emax)
    {
#if DECTRACE
      printf ("Adjusted exponent overflow [%d].\n", ae);
      decNumberShow (dn);
#endif
      return 1;
    }

  return 0;			/* it's OK */
}
#endif

#if DECALLOC
#undef malloc
#undef free
/* ------------------------------------------------------------------ */
/* decMalloc -- accountable allocation routine                        */
/*   n is the number of bytes to allocate                             */
/*                                                                    */
/* Semantics is the same as the stdlib malloc routine, but bytes      */
/* allocated are accounted for globally, and corruption fences are    */
/* added before and after the 'actual' storage.                       */
/* ------------------------------------------------------------------ */
/* This routine allocates storage with an extra twelve bytes; 8 are   */
/* at the start and hold:                                             */
/*   0-3 the original length requested                                */
/*   4-7 buffer corruption detection fence (DECFENCE, x4)             */
/* The 4 bytes at the end also hold a corruption fence (DECFENCE, x4) */
/* ------------------------------------------------------------------ */
static void *
decMalloc (uInt n)
{
  uInt size = n + 12;		/* true size */
  void *alloc;			/* -> allocated storage */
  uInt *j;			/* work */
  uByte *b, *b0;		/* .. */

  alloc = malloc (size);	/* -> allocated storage */
  if (alloc == NULL)
    return NULL;		/* out of strorage */
  b0 = (uByte *) alloc;		/* as bytes */
  decAllocBytes += n;		/* account for storage */
  j = (uInt *) alloc;		/* -> first four bytes */
  *j = n;			/* save n */
  /* printf("++ alloc(%d)\n", n); */
  for (b = b0 + 4; b < b0 + 8; b++)
    *b = DECFENCE;
  for (b = b0 + n + 8; b < b0 + n + 12; b++)
    *b = DECFENCE;
  return b0 + 8;		/* -> play area */
}

/* ------------------------------------------------------------------ */
/* decFree -- accountable free routine                                */
/*   alloc is the storage to free                                     */
/*                                                                    */
/* Semantics is the same as the stdlib malloc routine, except that    */
/* the global storage accounting is updated and the fences are        */
/* checked to ensure that no routine has written 'out of bounds'.     */
/* ------------------------------------------------------------------ */
/* This routine first checks that the fences have not been corrupted. */
/* It then frees the storage using the 'truw' storage address (that   */
/* is, offset by 8).                                                  */
/* ------------------------------------------------------------------ */
static void
decFree (void *alloc)
{
  uInt *j, n;			/* pointer, original length */
  uByte *b, *b0;		/* work */

  if (alloc == NULL)
    return;			/* allowed; it's a nop */
  b0 = (uByte *) alloc;		/* as bytes */
  b0 -= 8;			/* -> true start of storage */
  j = (uInt *) b0;		/* -> first four bytes */
  n = *j;			/* lift */
  for (b = b0 + 4; b < b0 + 8; b++)
    if (*b != DECFENCE)
      printf ("=== Corrupt byte [%02x] at offset %d from %d ===\n", *b,
	      b - b0 - 8, (Int) b0);
  for (b = b0 + n + 8; b < b0 + n + 12; b++)
    if (*b != DECFENCE)
      printf ("=== Corrupt byte [%02x] at offset +%d from %d, n=%d ===\n", *b,
	      b - b0 - 8, (Int) b0, n);
  free (b0);			/* drop the storage */
  decAllocBytes -= n;		/* account for storage */
}
#endif
