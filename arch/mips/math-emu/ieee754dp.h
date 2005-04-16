/*
 * IEEE754 floating point
 * double precision internal header file
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 * http://www.algor.co.uk
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


#include "ieee754int.h"

#define assert(expr) ((void)0)

/* 3bit extended double precision sticky right shift */
#define XDPSRS(v,rs)	\
  ((rs > (DP_MBITS+3))?1:((v) >> (rs)) | ((v) << (64-(rs)) != 0))

#define XDPSRSX1() \
  (xe++, (xm = (xm >> 1) | (xm & 1)))

#define XDPSRS1(v)	\
  (((v) >> 1) | ((v) & 1))

/* convert denormal to normalized with extended exponent */
#define DPDNORMx(m,e) \
  while( (m >> DP_MBITS) == 0) { m <<= 1; e--; }
#define DPDNORMX	DPDNORMx(xm,xe)
#define DPDNORMY	DPDNORMx(ym,ye)

static __inline ieee754dp builddp(int s, int bx, u64 m)
{
	ieee754dp r;

	assert((s) == 0 || (s) == 1);
	assert((bx) >= DP_EMIN - 1 + DP_EBIAS
	       && (bx) <= DP_EMAX + 1 + DP_EBIAS);
	assert(((m) >> DP_MBITS) == 0);

	r.parts.sign = s;
	r.parts.bexp = bx;
	r.parts.mant = m;
	return r;
}

extern int ieee754dp_isnan(ieee754dp);
extern int ieee754dp_issnan(ieee754dp);
extern int ieee754si_xcpt(int, const char *, ...);
extern s64 ieee754di_xcpt(s64, const char *, ...);
extern ieee754dp ieee754dp_xcpt(ieee754dp, const char *, ...);
extern ieee754dp ieee754dp_nanxcpt(ieee754dp, const char *, ...);
extern ieee754dp ieee754dp_bestnan(ieee754dp, ieee754dp);
extern ieee754dp ieee754dp_format(int, int, u64);


#define DPNORMRET2(s,e,m,name,a0,a1) \
{ \
    ieee754dp V = ieee754dp_format(s,e,m); \
    if(TSTX()) \
      return ieee754dp_xcpt(V,name,a0,a1); \
    else \
      return V; \
}

#define DPNORMRET1(s,e,m,name,a0)  DPNORMRET2(s,e,m,name,a0,a0)
