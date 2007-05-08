/*
 * File:         arch/blackfin/lib/muldi3.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef SI_TYPE_SIZE
#define SI_TYPE_SIZE 32
#endif
#define __ll_b (1L << (SI_TYPE_SIZE / 2))
#define __ll_lowpart(t) ((usitype) (t) % __ll_b)
#define __ll_highpart(t) ((usitype) (t) / __ll_b)
#define BITS_PER_UNIT 8

#if !defined(umul_ppmm)
#define umul_ppmm(w1, w0, u, v)						\
  do {									\
    usitype __x0, __x1, __x2, __x3;					\
    usitype __ul, __vl, __uh, __vh;					\
									\
    __ul = __ll_lowpart (u);						\
    __uh = __ll_highpart (u);						\
    __vl = __ll_lowpart (v);						\
    __vh = __ll_highpart (v);						\
									\
    __x0 = (usitype) __ul * __vl;					\
    __x1 = (usitype) __ul * __vh;					\
    __x2 = (usitype) __uh * __vl;					\
    __x3 = (usitype) __uh * __vh;					\
									\
    __x1 += __ll_highpart (__x0);/* this can't give carry */		\
    __x1 += __x2;		/* but this indeed can */		\
    if (__x1 < __x2)		/* did we get it? */			\
      __x3 += __ll_b;		/* yes, add it in the proper pos. */	\
									\
    (w1) = __x3 + __ll_highpart (__x1);					\
    (w0) = __ll_lowpart (__x1) * __ll_b + __ll_lowpart (__x0);		\
  } while (0)
#endif

#if !defined(__umulsidi3)
#define __umulsidi3(u, v) 						\
  ({diunion __w;                                                        \
       umul_ppmm (__w.s.high, __w.s.low, u, v);                         \
           __w.ll; })
#endif

typedef unsigned int usitype __attribute__ ((mode(SI)));
typedef int sitype __attribute__ ((mode(SI)));
typedef int ditype __attribute__ ((mode(DI)));
typedef int word_type __attribute__ ((mode(__word__)));

struct distruct {
	sitype low, high;
};
typedef union {
	struct distruct s;
	ditype ll;
} diunion;

#ifdef CONFIG_ARITHMETIC_OPS_L1
ditype __muldi3(ditype u, ditype v)__attribute__((l1_text));
#endif

ditype __muldi3(ditype u, ditype v)
{
	diunion w;
	diunion uu, vv;

	uu.ll = u, vv.ll = v;
	w.ll = __umulsidi3(uu.s.low, vv.s.low);
	w.s.high += ((usitype) uu.s.low * (usitype) vv.s.high
		     + (usitype) uu.s.high * (usitype) vv.s.low);

	return w.ll;
}
