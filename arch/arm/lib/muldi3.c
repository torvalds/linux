/* More subroutines needed by GCC output code on some machines.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1989, 92-98, 1999 Free Software Foundation, Inc.

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

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.
 */
/* support functions required by the kernel. based on code from gcc-2.95.3 */
/* I Molton     29/07/01 */

#include "gcclib.h"

#define umul_ppmm(xh, xl, a, b) \
{register u32 __t0, __t1, __t2;                                     \
  __asm__ ("%@ Inlined umul_ppmm					\n\
        mov     %2, %5, lsr #16						\n\
        mov     %0, %6, lsr #16						\n\
        bic     %3, %5, %2, lsl #16					\n\
        bic     %4, %6, %0, lsl #16					\n\
        mul     %1, %3, %4						\n\
        mul     %4, %2, %4						\n\
        mul     %3, %0, %3						\n\
        mul     %0, %2, %0						\n\
        adds    %3, %4, %3						\n\
        addcs   %0, %0, #65536						\n\
        adds    %1, %1, %3, lsl #16					\n\
        adc     %0, %0, %3, lsr #16"                                    \
           : "=&r" ((u32) (xh)),                                    \
             "=r" ((u32) (xl)),                                     \
             "=&r" (__t0), "=&r" (__t1), "=r" (__t2)                    \
           : "r" ((u32) (a)),                                       \
             "r" ((u32) (b)));}

#define __umulsidi3(u, v) \
  ({DIunion __w;                                                        \
    umul_ppmm (__w.s.high, __w.s.low, u, v);                            \
    __w.ll; })

s64 __muldi3(s64 u, s64 v)
{
	DIunion w;
	DIunion uu, vv;

	uu.ll = u, vv.ll = v;

	w.ll = __umulsidi3(uu.s.low, vv.s.low);
	w.s.high += ((u32) uu.s.low * (u32) vv.s.high
		     + (u32) uu.s.high * (u32) vv.s.low);

	return w.ll;
}
