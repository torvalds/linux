/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 * http://www.algor.co.uk
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
 *  Nov 7, 2000
 *  Modification to allow integration with Linux kernel
 *
 *  Kevin D. Kissell, kevink@mips.com and Carsten Langgard, carstenl@mips.com
 *  Copyright (C) 2000 MIPS Technologies, Inc. All rights reserved.
 */
#ifndef __ARCH_MIPS_MATH_EMU_IEEE754_H
#define __ARCH_MIPS_MATH_EMU_IEEE754_H

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/sched.h>

/*
 * Not very pretty, but the Linux kernel's normal va_list definition
 * does not allow it to be used as a structure element, as it is here.
 */
#ifndef _STDARG_H
#include <stdarg.h>
#endif

#ifdef __LITTLE_ENDIAN
struct ieee754dp_konst {
	unsigned mantlo:32;
	unsigned manthi:20;
	unsigned bexp:11;
	unsigned sign:1;
};
struct ieee754sp_konst {
	unsigned mant:23;
	unsigned bexp:8;
	unsigned sign:1;
};

typedef union _ieee754dp {
	struct ieee754dp_konst oparts;
	struct {
		u64 mant:52;
		unsigned int bexp:11;
		unsigned int sign:1;
	} parts;
	u64 bits;
	double d;
} ieee754dp;

typedef union _ieee754sp {
	struct ieee754sp_konst parts;
	float f;
	u32 bits;
} ieee754sp;
#endif

#ifdef __BIG_ENDIAN
struct ieee754dp_konst {
	unsigned sign:1;
	unsigned bexp:11;
	unsigned manthi:20;
	unsigned mantlo:32;
};

typedef union _ieee754dp {
	struct ieee754dp_konst oparts;
	struct {
		unsigned int sign:1;
		unsigned int bexp:11;
		u64 mant:52;
	} parts;
	double d;
	u64 bits;
} ieee754dp;

struct ieee754sp_konst {
	unsigned sign:1;
	unsigned bexp:8;
	unsigned mant:23;
};

typedef union _ieee754sp {
	struct ieee754sp_konst parts;
	float f;
	u32 bits;
} ieee754sp;
#endif

/*
 * single precision (often aka float)
*/
int ieee754sp_finite(ieee754sp x);
int ieee754sp_class(ieee754sp x);

ieee754sp ieee754sp_abs(ieee754sp x);
ieee754sp ieee754sp_neg(ieee754sp x);
ieee754sp ieee754sp_scalb(ieee754sp x, int);
ieee754sp ieee754sp_logb(ieee754sp x);

/* x with sign of y */
ieee754sp ieee754sp_copysign(ieee754sp x, ieee754sp y);

ieee754sp ieee754sp_add(ieee754sp x, ieee754sp y);
ieee754sp ieee754sp_sub(ieee754sp x, ieee754sp y);
ieee754sp ieee754sp_mul(ieee754sp x, ieee754sp y);
ieee754sp ieee754sp_div(ieee754sp x, ieee754sp y);

ieee754sp ieee754sp_fint(int x);
ieee754sp ieee754sp_funs(unsigned x);
ieee754sp ieee754sp_flong(s64 x);
ieee754sp ieee754sp_fulong(u64 x);
ieee754sp ieee754sp_fdp(ieee754dp x);

int ieee754sp_tint(ieee754sp x);
unsigned int ieee754sp_tuns(ieee754sp x);
s64 ieee754sp_tlong(ieee754sp x);
u64 ieee754sp_tulong(ieee754sp x);

int ieee754sp_cmp(ieee754sp x, ieee754sp y, int cop, int sig);
/*
 * basic sp math
 */
ieee754sp ieee754sp_modf(ieee754sp x, ieee754sp * ip);
ieee754sp ieee754sp_frexp(ieee754sp x, int *exp);
ieee754sp ieee754sp_ldexp(ieee754sp x, int exp);

ieee754sp ieee754sp_ceil(ieee754sp x);
ieee754sp ieee754sp_floor(ieee754sp x);
ieee754sp ieee754sp_trunc(ieee754sp x);

ieee754sp ieee754sp_sqrt(ieee754sp x);

/*
 * double precision (often aka double)
*/
int ieee754dp_finite(ieee754dp x);
int ieee754dp_class(ieee754dp x);

/* x with sign of y */
ieee754dp ieee754dp_copysign(ieee754dp x, ieee754dp y);

ieee754dp ieee754dp_add(ieee754dp x, ieee754dp y);
ieee754dp ieee754dp_sub(ieee754dp x, ieee754dp y);
ieee754dp ieee754dp_mul(ieee754dp x, ieee754dp y);
ieee754dp ieee754dp_div(ieee754dp x, ieee754dp y);

ieee754dp ieee754dp_abs(ieee754dp x);
ieee754dp ieee754dp_neg(ieee754dp x);
ieee754dp ieee754dp_scalb(ieee754dp x, int);

/* return exponent as integer in floating point format
 */
ieee754dp ieee754dp_logb(ieee754dp x);

ieee754dp ieee754dp_fint(int x);
ieee754dp ieee754dp_funs(unsigned x);
ieee754dp ieee754dp_flong(s64 x);
ieee754dp ieee754dp_fulong(u64 x);
ieee754dp ieee754dp_fsp(ieee754sp x);

ieee754dp ieee754dp_ceil(ieee754dp x);
ieee754dp ieee754dp_floor(ieee754dp x);
ieee754dp ieee754dp_trunc(ieee754dp x);

int ieee754dp_tint(ieee754dp x);
unsigned int ieee754dp_tuns(ieee754dp x);
s64 ieee754dp_tlong(ieee754dp x);
u64 ieee754dp_tulong(ieee754dp x);

int ieee754dp_cmp(ieee754dp x, ieee754dp y, int cop, int sig);
/*
 * basic sp math
 */
ieee754dp ieee754dp_modf(ieee754dp x, ieee754dp * ip);
ieee754dp ieee754dp_frexp(ieee754dp x, int *exp);
ieee754dp ieee754dp_ldexp(ieee754dp x, int exp);

ieee754dp ieee754dp_ceil(ieee754dp x);
ieee754dp ieee754dp_floor(ieee754dp x);
ieee754dp ieee754dp_trunc(ieee754dp x);

ieee754dp ieee754dp_sqrt(ieee754dp x);



/* 5 types of floating point number
*/
#define IEEE754_CLASS_NORM	0x00
#define IEEE754_CLASS_ZERO	0x01
#define IEEE754_CLASS_DNORM	0x02
#define IEEE754_CLASS_INF	0x03
#define IEEE754_CLASS_SNAN	0x04
#define IEEE754_CLASS_QNAN	0x05

/* exception numbers */
#define IEEE754_INEXACT			0x01
#define IEEE754_UNDERFLOW		0x02
#define IEEE754_OVERFLOW		0x04
#define IEEE754_ZERO_DIVIDE		0x08
#define IEEE754_INVALID_OPERATION	0x10

/* cmp operators
*/
#define IEEE754_CLT	0x01
#define IEEE754_CEQ	0x02
#define IEEE754_CGT	0x04
#define IEEE754_CUN	0x08

/* rounding mode
*/
#define IEEE754_RN	0	/* round to nearest */
#define IEEE754_RZ	1	/* round toward zero  */
#define IEEE754_RD	2	/* round toward -Infinity */
#define IEEE754_RU	3	/* round toward +Infinity */

/* other naming */
#define IEEE754_RM	IEEE754_RD
#define IEEE754_RP	IEEE754_RU

/* "normal" comparisons
*/
static inline int ieee754sp_eq(ieee754sp x, ieee754sp y)
{
	return ieee754sp_cmp(x, y, IEEE754_CEQ, 0);
}

static inline int ieee754sp_ne(ieee754sp x, ieee754sp y)
{
	return ieee754sp_cmp(x, y,
			     IEEE754_CLT | IEEE754_CGT | IEEE754_CUN, 0);
}

static inline int ieee754sp_lt(ieee754sp x, ieee754sp y)
{
	return ieee754sp_cmp(x, y, IEEE754_CLT, 0);
}

static inline int ieee754sp_le(ieee754sp x, ieee754sp y)
{
	return ieee754sp_cmp(x, y, IEEE754_CLT | IEEE754_CEQ, 0);
}

static inline int ieee754sp_gt(ieee754sp x, ieee754sp y)
{
	return ieee754sp_cmp(x, y, IEEE754_CGT, 0);
}


static inline int ieee754sp_ge(ieee754sp x, ieee754sp y)
{
	return ieee754sp_cmp(x, y, IEEE754_CGT | IEEE754_CEQ, 0);
}

static inline int ieee754dp_eq(ieee754dp x, ieee754dp y)
{
	return ieee754dp_cmp(x, y, IEEE754_CEQ, 0);
}

static inline int ieee754dp_ne(ieee754dp x, ieee754dp y)
{
	return ieee754dp_cmp(x, y,
			     IEEE754_CLT | IEEE754_CGT | IEEE754_CUN, 0);
}

static inline int ieee754dp_lt(ieee754dp x, ieee754dp y)
{
	return ieee754dp_cmp(x, y, IEEE754_CLT, 0);
}

static inline int ieee754dp_le(ieee754dp x, ieee754dp y)
{
	return ieee754dp_cmp(x, y, IEEE754_CLT | IEEE754_CEQ, 0);
}

static inline int ieee754dp_gt(ieee754dp x, ieee754dp y)
{
	return ieee754dp_cmp(x, y, IEEE754_CGT, 0);
}

static inline int ieee754dp_ge(ieee754dp x, ieee754dp y)
{
	return ieee754dp_cmp(x, y, IEEE754_CGT | IEEE754_CEQ, 0);
}


/*
 * Like strtod
 */
ieee754dp ieee754dp_fstr(const char *s, char **endp);
char *ieee754dp_tstr(ieee754dp x, int prec, int fmt, int af);


/*
 * The control status register
 */
struct _ieee754_csr {
#ifdef __BIG_ENDIAN
	unsigned pad0:7;
	unsigned nod:1;		/* set 1 for no denormalised numbers */
	unsigned c:1;		/* condition */
	unsigned pad1:5;
	unsigned cx:6;		/* exceptions this operation */
	unsigned mx:5;		/* exception enable  mask */
	unsigned sx:5;		/* exceptions total */
	unsigned rm:2;		/* current rounding mode */
#endif
#ifdef __LITTLE_ENDIAN
	unsigned rm:2;		/* current rounding mode */
	unsigned sx:5;		/* exceptions total */
	unsigned mx:5;		/* exception enable  mask */
	unsigned cx:6;		/* exceptions this operation */
	unsigned pad1:5;
	unsigned c:1;		/* condition */
	unsigned nod:1;		/* set 1 for no denormalised numbers */
	unsigned pad0:7;
#endif
};
#define ieee754_csr (*(struct _ieee754_csr *)(&current->thread.fpu.soft.fcr31))

static inline unsigned ieee754_getrm(void)
{
	return (ieee754_csr.rm);
}
static inline unsigned ieee754_setrm(unsigned rm)
{
	return (ieee754_csr.rm = rm);
}

/*
 * get current exceptions
 */
static inline unsigned ieee754_getcx(void)
{
	return (ieee754_csr.cx);
}

/* test for current exception condition
 */
static inline int ieee754_cxtest(unsigned n)
{
	return (ieee754_csr.cx & n);
}

/*
 * get sticky exceptions
 */
static inline unsigned ieee754_getsx(void)
{
	return (ieee754_csr.sx);
}

/* clear sticky conditions
*/
static inline unsigned ieee754_clrsx(void)
{
	return (ieee754_csr.sx = 0);
}

/* test for sticky exception condition
 */
static inline int ieee754_sxtest(unsigned n)
{
	return (ieee754_csr.sx & n);
}

/* debugging */
ieee754sp ieee754sp_dump(char *s, ieee754sp x);
ieee754dp ieee754dp_dump(char *s, ieee754dp x);

#define IEEE754_SPCVAL_PZERO	0
#define IEEE754_SPCVAL_NZERO	1
#define IEEE754_SPCVAL_PONE	2
#define IEEE754_SPCVAL_NONE	3
#define IEEE754_SPCVAL_PTEN	4
#define IEEE754_SPCVAL_NTEN	5
#define IEEE754_SPCVAL_PINFINITY	6
#define IEEE754_SPCVAL_NINFINITY	7
#define IEEE754_SPCVAL_INDEF	8
#define IEEE754_SPCVAL_PMAX	9	/* +max norm */
#define IEEE754_SPCVAL_NMAX	10	/* -max norm */
#define IEEE754_SPCVAL_PMIN	11	/* +min norm */
#define IEEE754_SPCVAL_NMIN	12	/* +min norm */
#define IEEE754_SPCVAL_PMIND	13	/* +min denorm */
#define IEEE754_SPCVAL_NMIND	14	/* +min denorm */
#define IEEE754_SPCVAL_P1E31	15	/* + 1.0e31 */
#define IEEE754_SPCVAL_P1E63	16	/* + 1.0e63 */

extern const struct ieee754dp_konst __ieee754dp_spcvals[];
extern const struct ieee754sp_konst __ieee754sp_spcvals[];
#define ieee754dp_spcvals ((const ieee754dp *)__ieee754dp_spcvals)
#define ieee754sp_spcvals ((const ieee754sp *)__ieee754sp_spcvals)

/*
 * Return infinity with given sign
 */
#define ieee754dp_inf(sn)     (ieee754dp_spcvals[IEEE754_SPCVAL_PINFINITY+(sn)])
#define ieee754dp_zero(sn)	(ieee754dp_spcvals[IEEE754_SPCVAL_PZERO+(sn)])
#define ieee754dp_one(sn)	(ieee754dp_spcvals[IEEE754_SPCVAL_PONE+(sn)])
#define ieee754dp_ten(sn)	(ieee754dp_spcvals[IEEE754_SPCVAL_PTEN+(sn)])
#define ieee754dp_indef()	(ieee754dp_spcvals[IEEE754_SPCVAL_INDEF])
#define ieee754dp_max(sn)	(ieee754dp_spcvals[IEEE754_SPCVAL_PMAX+(sn)])
#define ieee754dp_min(sn)	(ieee754dp_spcvals[IEEE754_SPCVAL_PMIN+(sn)])
#define ieee754dp_mind(sn)	(ieee754dp_spcvals[IEEE754_SPCVAL_PMIND+(sn)])
#define ieee754dp_1e31()	(ieee754dp_spcvals[IEEE754_SPCVAL_P1E31])
#define ieee754dp_1e63()	(ieee754dp_spcvals[IEEE754_SPCVAL_P1E63])

#define ieee754sp_inf(sn)     (ieee754sp_spcvals[IEEE754_SPCVAL_PINFINITY+(sn)])
#define ieee754sp_zero(sn)	(ieee754sp_spcvals[IEEE754_SPCVAL_PZERO+(sn)])
#define ieee754sp_one(sn)	(ieee754sp_spcvals[IEEE754_SPCVAL_PONE+(sn)])
#define ieee754sp_ten(sn)	(ieee754sp_spcvals[IEEE754_SPCVAL_PTEN+(sn)])
#define ieee754sp_indef()	(ieee754sp_spcvals[IEEE754_SPCVAL_INDEF])
#define ieee754sp_max(sn)	(ieee754sp_spcvals[IEEE754_SPCVAL_PMAX+(sn)])
#define ieee754sp_min(sn)	(ieee754sp_spcvals[IEEE754_SPCVAL_PMIN+(sn)])
#define ieee754sp_mind(sn)	(ieee754sp_spcvals[IEEE754_SPCVAL_PMIND+(sn)])
#define ieee754sp_1e31()	(ieee754sp_spcvals[IEEE754_SPCVAL_P1E31])
#define ieee754sp_1e63()	(ieee754sp_spcvals[IEEE754_SPCVAL_P1E63])

/*
 * Indefinite integer value
 */
#define ieee754si_indef()	INT_MAX
#ifdef LONG_LONG_MAX
#define ieee754di_indef()	LONG_LONG_MAX
#else
#define ieee754di_indef()	((s64)(~0ULL>>1))
#endif

/* IEEE exception context, passed to handler */
struct ieee754xctx {
	const char *op;		/* operation name */
	int rt;			/* result type */
	union {
		ieee754sp sp;	/* single precision */
		ieee754dp dp;	/* double precision */
#ifdef IEEE854_XP
		ieee754xp xp;	/* extended precision */
#endif
		int si;		/* standard signed integer (32bits) */
		s64 di;		/* extended signed integer (64bits) */
	} rv;			/* default result format implied by op */
	va_list ap;
};

/* result types for xctx.rt */
#define IEEE754_RT_SP	0
#define IEEE754_RT_DP	1
#define IEEE754_RT_XP	2
#define IEEE754_RT_SI	3
#define IEEE754_RT_DI	4

extern void ieee754_xcpt(struct ieee754xctx *xcp);

/* compat */
#define ieee754dp_fix(x)	ieee754dp_tint(x)
#define ieee754sp_fix(x)	ieee754sp_tint(x)

#endif /* __ARCH_MIPS_MATH_EMU_IEEE754_H */
