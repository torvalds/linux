/* lshrdi3.c extracted from gcc-2.7.2/libgcc2.c which is: */
/* Copyright (C) 1989, 1992, 1993, 1994, 1995 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details. */

#include <linux/compiler.h>
#include <linux/export.h>

#define BITS_PER_UNIT 8

typedef		 int SItype	__mode(SI);
typedef unsigned int USItype	__mode(SI);
typedef		 int DItype	__mode(DI);
typedef int word_type           __mode(__word__);

struct DIstruct {SItype high, low;};

typedef union
{
  struct DIstruct s;
  DItype ll;
} DIunion;

DItype
__lshrdi3 (DItype u, word_type b)
{
  DIunion w;
  word_type bm;
  DIunion uu;

  if (b == 0)
    return u;

  uu.ll = u;

  bm = (sizeof (SItype) * BITS_PER_UNIT) - b;
  if (bm <= 0)
    {
      w.s.high = 0;
      w.s.low = (USItype)uu.s.high >> -bm;
    }
  else
    {
      USItype carries = (USItype)uu.s.high << bm;
      w.s.high = (USItype)uu.s.high >> b;
      w.s.low = ((USItype)uu.s.low >> b) | carries;
    }

  return w.ll;
}
EXPORT_SYMBOL(__lshrdi3);
