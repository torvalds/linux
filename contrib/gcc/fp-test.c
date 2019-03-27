/* fp-test.c - Check that all floating-point operations are available.
   Copyright (C) 1995, 2000, 2003 Free Software Foundation, Inc.
   Contributed by Ronald F. Guilmette <rfg@monkeys.com>.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This is a trivial test program which may be useful to people who are
   porting the GCC or G++ compilers to a new system.  The intent here is
   merely to check that all floating-point operations have been provided
   by the port. (Note that I say ``provided'' rather than ``implemented''.)

   To use this file, simply compile it (with GCC or G++) and then try to
   link it in the normal way (also using GCC or G++ respectively).  If
   all of the floating -point operations (including conversions) have
   been provided, then this file will link without incident.  If however
   one or more of the primitive floating-point operations have not been
   properly provided, you will get link-time errors indicating which
   floating-point operations are unavailable.

   This file will typically be used when porting the GNU compilers to
   some system which lacks floating-point hardware, and for which
   software emulation routines (for FP ops) are needed in order to
   complete the port.  */

#if 0
#include <math.h>
#endif

extern double acos (double);
extern double asin (double);
extern double atan (double);
extern double atan2 (double, double);
extern double cos (double);
extern double sin (double);
extern double tan (double);
extern double cosh (double);
extern double sinh (double);
extern double tanh (double);
extern double exp (double);
extern double frexp (double, int *);
extern double ldexp (double, int);
extern double log (double);
extern double log10 (double);
extern double modf (double, double *);
extern double pow (double, double);
extern double sqrt (double);
extern double ceil (double);
extern double fabs (double);
extern double floor (double);
extern double fmod (double, double);

int i1, i2 = 2;

volatile signed char sc;
volatile unsigned char uc;

volatile signed short ss;
volatile unsigned short us;

volatile signed int si;
volatile unsigned int ui;

volatile signed long sl;
volatile unsigned long ul;

volatile float f1 = 1.0, f2 = 1.0, f3 = 1.0;
volatile double d1 = 1.0, d2 = 1.0, d3 = 1.0;
volatile long double D1 = 1.0, D2 = 1.0, D3 = 1.0;

int
main (void)
{
  /* TYPE: float */

  f1 = -f2;
  f1 = f2 + f3;
  f1 = f2 - f3;
  f1 = f2 * f3;
  f1 = f2 / f3;
  f1 += f2;
  f1 -= f2;
  f1 *= f2;
  f1 /= f2;

  si = f1 == f2;
  si = f1 != f2;
  si = f1 > f2;
  si = f1 < f2;
  si = f1 >= f2;
  si = f1 <= f2;

  si = __builtin_isgreater (f1, f2);
  si = __builtin_isgreaterequal (f1, f2);
  si = __builtin_isless (f1, f2);
  si = __builtin_islessequal (f1, f2);
  si = __builtin_islessgreater (f1, f2);
  si = __builtin_isunordered (f1, f2);

  sc = f1;
  uc = f1;
  ss = f1;
  us = f1;
  si = f1;
  ui = f1;
  sl = f1;
  ul = f1;
  d1 = f1;
  D1 = f1;

  f1 = sc;
  f1 = uc;
  f1 = ss;
  f1 = us;
  f1 = si;
  f1 = ui;
  f1 = sl;
  f1 = ul;
  f1 = d1;
  f1 = D1;

  d1 = -d2;
  d1 = d2 + d3;
  d1 = d2 - d3;
  d1 = d2 * d3;
  d1 = d2 / d3;
  d1 += d2;
  d1 -= d2;
  d1 *= d2;
  d1 /= d2;

  si = d1 == d2;
  si = d1 != d2;
  si = d1 > d2;
  si = d1 < d2;
  si = d1 >= d2;
  si = d1 <= d2;

  si = __builtin_isgreater (d1, d2);
  si = __builtin_isgreaterequal (d1, d2);
  si = __builtin_isless (d1, d2);
  si = __builtin_islessequal (d1, d2);
  si = __builtin_islessgreater (d1, d2);
  si = __builtin_isunordered (d1, d2);

  sc = d1;
  uc = d1;
  ss = d1;
  us = d1;
  si = d1;
  ui = d1;
  sl = d1;
  ul = d1;
  f1 = d1;
  D1 = d1;

  d1 = sc;
  d1 = uc;
  d1 = ss;
  d1 = us;
  d1 = si;
  d1 = ui;
  d1 = sl;
  d1 = ul;
  d1 = f1;
  d1 = D1;

  D1 = -D2;
  D1 = D2 + D3;
  D1 = D2 - D3;
  D1 = D2 * D3;
  D1 = D2 / D3;
  D1 += D2;
  D1 -= D2;
  D1 *= D2;
  D1 /= D2;

  si = D1 == D2;
  si = D1 != D2;
  si = D1 > D2;
  si = D1 < D2;
  si = D1 >= D2;
  si = D1 <= D2;

  si = __builtin_isgreater (D1, D2);
  si = __builtin_isgreaterequal (D1, D2);
  si = __builtin_isless (D1, D2);
  si = __builtin_islessequal (D1, D2);
  si = __builtin_islessgreater (D1, D2);
  si = __builtin_isunordered (D1, D2);

  sc = D1;
  uc = D1;
  ss = D1;
  us = D1;
  si = D1;
  ui = D1;
  sl = D1;
  ul = D1;
  f1 = D1;
  d1 = D1;

  D1 = sc;
  D1 = uc;
  D1 = ss;
  D1 = us;
  D1 = si;
  D1 = ui;
  D1 = sl;
  D1 = ul;
  D1 = f1;
  D1 = d1;

  d1 = acos (d2);
  d1 = asin (d2);
  d1 = atan (d2);
  d1 = atan2 (d2, d3);
  d1 = cos (d2);
  d1 = sin (d2);
  d1 = tan (d2);
  d1 = cosh (d2);
  d1 = sinh (d2);
  d1 = tanh (d2);
  d1 = exp (d2);
  d1 = frexp (d2, &i1);
  d1 = ldexp (d2, i2);
  d1 = log (d2);
  d1 = log10 (d2);
  d1 = modf (d2, &d3);
  d1 = pow (d2, d3);
  d1 = sqrt (d2);
  d1 = ceil (d2);
  d1 = fabs (d2);
  d1 = floor (d2);
  d1 = fmod (d2, d3);

  return 0;
}
