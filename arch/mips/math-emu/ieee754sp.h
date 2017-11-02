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

/* 64 bit right shift with rounding */
#define XSPSRS64(v, rs)						\
	(((rs) >= 64) ? ((v) != 0) : ((v) >> (rs)) | ((v) << (64-(rs)) != 0))

/* 3bit extended single precision sticky right shift */
#define XSPSRS(v, rs)						\
	((rs > (SP_FBITS+3))?1:((v) >> (rs)) | ((v) << (32-(rs)) != 0))

#define XSPSRS1(m) \
	((m >> 1) | (m & 1))

#define SPXSRSX1() \
	(xe++, (xm = XSPSRS1(xm)))

#define SPXSRSY1() \
	(ye++, (ym = XSPSRS1(ym)))

/* convert denormal to normalized with extended exponent */
#define SPDNORMx(m,e) \
	while ((m >> SP_FBITS) == 0) { m <<= 1; e--; }
#define SPDNORMX	SPDNORMx(xm, xe)
#define SPDNORMY	SPDNORMx(ym, ye)
#define SPDNORMZ	SPDNORMx(zm, ze)

static inline union ieee754sp buildsp(int s, int bx, unsigned int m)
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

extern union ieee754sp __cold ieee754sp_nanxcpt(union ieee754sp);
extern union ieee754sp ieee754sp_format(int, int, unsigned);
