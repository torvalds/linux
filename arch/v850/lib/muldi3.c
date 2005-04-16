/* muldi3.c extracted from gcc-2.7.2.3/libgcc2.c and 
			   gcc-2.7.2.3/longlong.h which is: */
/* Copyright (C) 1989, 1992, 1993, 1994, 1995, 2001 Free Software Foundation, Inc.

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

#define umul_ppmm(w1, w0, u, v) \
  __asm__ ("mulu %3, %0, %1"						\
           : "=r" ((USItype)(w0)),					\
             "=r" ((USItype)(w1))					\
           : "%0" ((USItype)(u)),					\
             "r" ((USItype)(v)))

#define __umulsidi3(u, v) \
  ({DIunion __w;							\
    umul_ppmm (__w.s.high, __w.s.low, u, v);				\
    __w.ll; })

typedef 	 int SItype	__attribute__ ((mode (SI)));
typedef unsigned int USItype	__attribute__ ((mode (SI)));
typedef		 int DItype	__attribute__ ((mode (DI)));
typedef int word_type __attribute__ ((mode (__word__)));

struct DIstruct {SItype high, low;};

typedef union
{
  struct DIstruct s;
  DItype ll;
} DIunion;

DItype
__muldi3 (DItype u, DItype v)
{
  DIunion w;
  DIunion uu, vv;

  uu.ll = u,
  vv.ll = v;

  w.ll = __umulsidi3 (uu.s.low, vv.s.low);
  w.s.high += ((USItype) uu.s.low * (USItype) vv.s.high
	       + (USItype) uu.s.high * (USItype) vv.s.low);

  return w.ll;
}
