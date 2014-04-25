/*
 * IEEE754 floating point
 * double precision internal header file
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
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
 *  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
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

#define SPSIGN(sp)	(sp.sign)
#define SPBEXP(sp)	(sp.bexp)
#define SPMANT(sp)	(sp.mant)

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

	r.sign = s;
	r.bexp = bx;
	r.mant = m;

	return r;
}

extern int ieee754sp_isnan(union ieee754sp);
extern union ieee754sp __cold ieee754sp_nanxcpt(union ieee754sp);
extern union ieee754sp ieee754sp_format(int, int, unsigned);
