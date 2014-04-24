/*
 * IEEE754 floating point
 * double precision internal header file
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */

#include <linux/compiler.h>

#include "ieee754int.h"

#define assert(expr) ((void)0)

#define SP_EBIAS	127
#define SP_EMIN		(-126)
#define SP_EMAX		127
#define SP_FBITS	23
#define SP_MBITS	23

#define SP_MBIT(x)	((u32)1 << (x))
#define SP_HIDDEN_BIT	SP_MBIT(SP_FBITS)
#define SP_SIGN_BIT	SP_MBIT(31)

#define SPSIGN(sp)	(sp.parts.sign)
#define SPBEXP(sp)	(sp.parts.bexp)
#define SPMANT(sp)	(sp.parts.mant)

static inline int ieee754sp_finite(union ieee754sp x)
{
	return SPBEXP(x) != SP_EMAX + 1 + SP_EBIAS;
}

/* 3bit extended single precision sticky right shift */
#define SPXSRSXn(rs)							\
	(xe += rs,							\
	 xm = (rs > (SP_FBITS+3))?1:((xm) >> (rs)) | ((xm) << (32-(rs)) != 0))

#define SPXSRSX1() \
	(xe++, (xm = (xm >> 1) | (xm & 1)))

#define SPXSRSYn(rs)								\
	(ye+=rs,								\
	 ym = (rs > (SP_FBITS+3))?1:((ym) >> (rs)) | ((ym) << (32-(rs)) != 0))

#define SPXSRSY1() \
	(ye++, (ym = (ym >> 1) | (ym & 1)))

/* convert denormal to normalized with extended exponent */
#define SPDNORMx(m,e) \
	while ((m >> SP_FBITS) == 0) { m <<= 1; e--; }
#define SPDNORMX	SPDNORMx(xm, xe)
#define SPDNORMY	SPDNORMx(ym, ye)

static inline union ieee754sp buildsp(int s, int bx, unsigned m)
{
	union ieee754sp r;

	assert((s) == 0 || (s) == 1);
	assert((bx) >= SP_EMIN - 1 + SP_EBIAS
	       && (bx) <= SP_EMAX + 1 + SP_EBIAS);
	assert(((m) >> SP_FBITS) == 0);

	r.parts.sign = s;
	r.parts.bexp = bx;
	r.parts.mant = m;

	return r;
}

extern int ieee754sp_isnan(union ieee754sp);
extern int __cold ieee754si_xcpt(int, const char *, ...);
extern s64 __cold ieee754di_xcpt(s64, const char *, ...);
extern union ieee754sp __cold ieee754sp_xcpt(union ieee754sp, const char *, ...);
extern union ieee754sp __cold ieee754sp_nanxcpt(union ieee754sp, const char *, ...);
extern union ieee754sp ieee754sp_format(int, int, unsigned);


#define SPNORMRET2(s, e, m, name, a0, a1)				\
{									\
	union ieee754sp V = ieee754sp_format(s, e, m);			\
									\
	if (ieee754_tstx())						\
		return ieee754sp_xcpt(V, name, a0, a1);			\
	else								\
		return V;						\
}

#define SPNORMRET1(s, e, m, name, a0)  SPNORMRET2(s, e, m, name, a0, a0)
