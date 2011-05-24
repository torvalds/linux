/*
 * IEEE754 floating point
 * common internal header file
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


#include "ieee754.h"

#define DP_EBIAS	1023
#define DP_EMIN		(-1022)
#define DP_EMAX		1023
#define DP_MBITS	52

#define SP_EBIAS	127
#define SP_EMIN		(-126)
#define SP_EMAX		127
#define SP_MBITS	23

#define DP_MBIT(x)	((u64)1 << (x))
#define DP_HIDDEN_BIT	DP_MBIT(DP_MBITS)
#define DP_SIGN_BIT	DP_MBIT(63)

#define SP_MBIT(x)	((u32)1 << (x))
#define SP_HIDDEN_BIT	SP_MBIT(SP_MBITS)
#define SP_SIGN_BIT	SP_MBIT(31)


#define SPSIGN(sp)	(sp.parts.sign)
#define SPBEXP(sp)	(sp.parts.bexp)
#define SPMANT(sp)	(sp.parts.mant)

#define DPSIGN(dp)	(dp.parts.sign)
#define DPBEXP(dp)	(dp.parts.bexp)
#define DPMANT(dp)	(dp.parts.mant)

#define CLPAIR(x, y)	((x)*6+(y))

#define CLEARCX	\
  (ieee754_csr.cx = 0)

#define SETCX(x) \
  (ieee754_csr.cx |= (x), ieee754_csr.sx |= (x))

#define SETANDTESTCX(x) \
  (SETCX(x), ieee754_csr.mx & (x))

#define TSTX()	\
	(ieee754_csr.cx & ieee754_csr.mx)


#define COMPXSP \
  unsigned xm; int xe; int xs __maybe_unused; int xc

#define COMPYSP \
  unsigned ym; int ye; int ys; int yc

#define EXPLODESP(v, vc, vs, ve, vm) \
{\
    vs = SPSIGN(v);\
    ve = SPBEXP(v);\
    vm = SPMANT(v);\
    if(ve == SP_EMAX+1+SP_EBIAS){\
	if(vm == 0)\
	  vc = IEEE754_CLASS_INF;\
	else if(vm & SP_MBIT(SP_MBITS-1)) \
	  vc = IEEE754_CLASS_SNAN;\
	else \
	  vc = IEEE754_CLASS_QNAN;\
    } else if(ve == SP_EMIN-1+SP_EBIAS) {\
	if(vm) {\
	    ve = SP_EMIN;\
	    vc = IEEE754_CLASS_DNORM;\
	} else\
	  vc = IEEE754_CLASS_ZERO;\
    } else {\
	ve -= SP_EBIAS;\
	vm |= SP_HIDDEN_BIT;\
	vc = IEEE754_CLASS_NORM;\
    }\
}
#define EXPLODEXSP EXPLODESP(x, xc, xs, xe, xm)
#define EXPLODEYSP EXPLODESP(y, yc, ys, ye, ym)


#define COMPXDP \
u64 xm; int xe; int xs __maybe_unused; int xc

#define COMPYDP \
u64 ym; int ye; int ys; int yc

#define EXPLODEDP(v, vc, vs, ve, vm) \
{\
    vm = DPMANT(v);\
    vs = DPSIGN(v);\
    ve = DPBEXP(v);\
    if(ve == DP_EMAX+1+DP_EBIAS){\
	if(vm == 0)\
	  vc = IEEE754_CLASS_INF;\
	else if(vm & DP_MBIT(DP_MBITS-1)) \
	  vc = IEEE754_CLASS_SNAN;\
	else \
	  vc = IEEE754_CLASS_QNAN;\
    } else if(ve == DP_EMIN-1+DP_EBIAS) {\
	if(vm) {\
	    ve = DP_EMIN;\
	    vc = IEEE754_CLASS_DNORM;\
	} else\
	  vc = IEEE754_CLASS_ZERO;\
    } else {\
	ve -= DP_EBIAS;\
	vm |= DP_HIDDEN_BIT;\
	vc = IEEE754_CLASS_NORM;\
    }\
}
#define EXPLODEXDP EXPLODEDP(x, xc, xs, xe, xm)
#define EXPLODEYDP EXPLODEDP(y, yc, ys, ye, ym)

#define FLUSHDP(v, vc, vs, ve, vm) \
	if(vc==IEEE754_CLASS_DNORM) {\
	    if(ieee754_csr.nod) {\
		SETCX(IEEE754_INEXACT);\
		vc = IEEE754_CLASS_ZERO;\
		ve = DP_EMIN-1+DP_EBIAS;\
		vm = 0;\
		v = ieee754dp_zero(vs);\
	    }\
	}

#define FLUSHSP(v, vc, vs, ve, vm) \
	if(vc==IEEE754_CLASS_DNORM) {\
	    if(ieee754_csr.nod) {\
		SETCX(IEEE754_INEXACT);\
		vc = IEEE754_CLASS_ZERO;\
		ve = SP_EMIN-1+SP_EBIAS;\
		vm = 0;\
		v = ieee754sp_zero(vs);\
	    }\
	}

#define FLUSHXDP FLUSHDP(x, xc, xs, xe, xm)
#define FLUSHYDP FLUSHDP(y, yc, ys, ye, ym)
#define FLUSHXSP FLUSHSP(x, xc, xs, xe, xm)
#define FLUSHYSP FLUSHSP(y, yc, ys, ye, ym)
