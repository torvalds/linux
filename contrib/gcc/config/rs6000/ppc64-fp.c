/* Functions needed for soft-float on powerpc64-linux, copied from
   libgcc2.c with macros expanded to force the use of specific types.
 
   Copyright (C) 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2006  Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#if defined(__powerpc64__) || defined (__64BIT__) || defined(__ppc64__)
#define TMODES
#include "config/fp-bit.h"

extern DItype __fixtfdi (TFtype);
extern DItype __fixdfdi (DFtype);
extern DItype __fixsfdi (SFtype);
extern USItype __fixunsdfsi (DFtype);
extern USItype __fixunssfsi (SFtype);
extern TFtype __floatditf (DItype);
extern TFtype __floatunditf (UDItype);
extern DFtype __floatdidf (DItype);
extern DFtype __floatundidf (UDItype);
extern SFtype __floatdisf (DItype);
extern SFtype __floatundisf (UDItype);
extern DItype __fixunstfdi (TFtype);

static DItype local_fixunssfdi (SFtype);
static DItype local_fixunsdfdi (DFtype);

DItype
__fixtfdi (TFtype a)
{
  if (a < 0)
    return - __fixunstfdi (-a);
  return __fixunstfdi (a);
}

DItype
__fixdfdi (DFtype a)
{
  if (a < 0)
    return - local_fixunsdfdi (-a);
  return local_fixunsdfdi (a);
}

DItype
__fixsfdi (SFtype a)
{
  if (a < 0)
    return - local_fixunssfdi (-a);
  return local_fixunssfdi (a);
}

USItype
__fixunsdfsi (DFtype a)
{
  if (a >= - (DFtype) (- ((SItype)(((USItype)1 << ((4 * 8) - 1)) - 1)) - 1))
    return (SItype) (a + (- ((SItype)(((USItype)1 << ((4 * 8) - 1)) - 1)) - 1))
                       - (- ((SItype)(((USItype)1 << ((4 * 8) - 1)) - 1)) - 1);
  return (SItype) a;
}

USItype
__fixunssfsi (SFtype a)
{
  if (a >= - (SFtype) (- ((SItype)(((USItype)1 << ((4 * 8) - 1)) - 1)) - 1))
    return (SItype) (a + (- ((SItype)(((USItype)1 << ((4 * 8) - 1)) - 1)) - 1))
                       - (- ((SItype)(((USItype)1 << ((4 * 8) - 1)) - 1)) - 1);
  return (SItype) a;
}

TFtype
__floatditf (DItype u)
{
  DFtype dh, dl;

  dh = (SItype) (u >> (sizeof (SItype) * 8));
  dh *= 2.0 * (((UDItype) 1) << ((sizeof (SItype) * 8) - 1));
  dl = (USItype) (u & ((((UDItype) 1) << (sizeof (SItype) * 8)) - 1));

  return (TFtype) dh + (TFtype) dl;
}

TFtype
__floatunditf (UDItype u)
{
  DFtype dh, dl;

  dh = (USItype) (u >> (sizeof (SItype) * 8));
  dh *= 2.0 * (((UDItype) 1) << ((sizeof (SItype) * 8) - 1));
  dl = (USItype) (u & ((((UDItype) 1) << (sizeof (SItype) * 8)) - 1));

  return (TFtype) dh + (TFtype) dl;
}

DFtype
__floatdidf (DItype u)
{
  DFtype d;

  d = (SItype) (u >> (sizeof (SItype) * 8));
  d *= 2.0 * (((UDItype) 1) << ((sizeof (SItype) * 8) - 1));
  d += (USItype) (u & ((((UDItype) 1) << (sizeof (SItype) * 8)) - 1));

  return d;
}

DFtype
__floatundidf (UDItype u)
{
  DFtype d;

  d = (USItype) (u >> (sizeof (SItype) * 8));
  d *= 2.0 * (((UDItype) 1) << ((sizeof (SItype) * 8) - 1));
  d += (USItype) (u & ((((UDItype) 1) << (sizeof (SItype) * 8)) - 1));

  return d;
}

SFtype
__floatdisf (DItype u)
{
  DFtype f;

  if (53 < (sizeof (DItype) * 8)
      && 53 > ((sizeof (DItype) * 8) - 53 + 24))
    {
      if (! (- ((DItype) 1 << 53) < u
             && u < ((DItype) 1 << 53)))
        {
          if ((UDItype) u & (((UDItype) 1 << ((sizeof (DItype) * 8) - 53)) - 1))
            {
              u &= ~ (((UDItype) 1 << ((sizeof (DItype) * 8) - 53)) - 1);
              u |= ((UDItype) 1 << ((sizeof (DItype) * 8) - 53));
            }
        }
    }
  f = (SItype) (u >> (sizeof (SItype) * 8));
  f *= 2.0 * (((UDItype) 1) << ((sizeof (SItype) * 8) - 1));
  f += (USItype) (u & ((((UDItype) 1) << (sizeof (SItype) * 8)) - 1));

  return (SFtype) f;
}

SFtype
__floatundisf (UDItype u)
{
  DFtype f;

  if (53 < (sizeof (DItype) * 8)
      && 53 > ((sizeof (DItype) * 8) - 53 + 24))
    {
      if (u >= ((UDItype) 1 << 53))
        {
          if ((UDItype) u & (((UDItype) 1 << ((sizeof (DItype) * 8) - 53)) - 1))
            {
              u &= ~ (((UDItype) 1 << ((sizeof (DItype) * 8) - 53)) - 1);
              u |= ((UDItype) 1 << ((sizeof (DItype) * 8) - 53));
            }
        }
    }
  f = (USItype) (u >> (sizeof (SItype) * 8));
  f *= 2.0 * (((UDItype) 1) << ((sizeof (SItype) * 8) - 1));
  f += (USItype) (u & ((((UDItype) 1) << (sizeof (SItype) * 8)) - 1));

  return (SFtype) f;
}

DItype
__fixunstfdi (TFtype a)
{
  if (a < 0)
    return 0;

  /* Compute high word of result, as a flonum.  */
  const TFtype b = (a / (((UDItype) 1) << (sizeof (SItype) * 8)));
  /* Convert that to fixed (but not to DItype!),
     and shift it into the high word.  */
  UDItype v = (USItype) b;
  v <<= (sizeof (SItype) * 8);
  /* Remove high part from the TFtype, leaving the low part as flonum.  */
  a -= (TFtype) v;
  /* Convert that to fixed (but not to DItype!) and add it in.
     Sometimes A comes out negative.  This is significant, since
     A has more bits than a long int does.  */
  if (a < 0)
    v -= (USItype) (-a);
  else
    v += (USItype) a;
  return v;
}

/* This version is needed to prevent recursion; fixunsdfdi in libgcc
   calls fixdfdi, which in turn calls calls fixunsdfdi.  */

static DItype
local_fixunsdfdi (DFtype a)
{
  USItype hi, lo;

  hi = a / (((UDItype) 1) << (sizeof (SItype) * 8));
  lo = (a - ((DFtype) hi) * (((UDItype) 1) << (sizeof (SItype) * 8)));
  return ((UDItype) hi << (sizeof (SItype) * 8)) | lo;
}

/* This version is needed to prevent recursion; fixunssfdi in libgcc
   calls fixsfdi, which in turn calls calls fixunssfdi.  */

static DItype
local_fixunssfdi (SFtype original_a)
{
  DFtype a = original_a;
  USItype hi, lo;

  hi = a / (((UDItype) 1) << (sizeof (SItype) * 8));
  lo = (a - ((DFtype) hi) * (((UDItype) 1) << (sizeof (SItype) * 8)));
  return ((UDItype) hi << (sizeof (SItype) * 8)) | lo;
}

#endif /* __powerpc64__ */
