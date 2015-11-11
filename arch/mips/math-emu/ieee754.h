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
 *
 *  Nov 7, 2000
 *  Modification to allow integration with Linux kernel
 *
 *  Kevin D. Kissell, kevink@mips.com and Carsten Langgard, carstenl@mips.com
 *  Copyright (C) 2000 MIPS Technologies, Inc. All rights reserved.
 */
#ifndef __ARCH_MIPS_MATH_EMU_IEEE754_H
#define __ARCH_MIPS_MATH_EMU_IEEE754_H

#include <linux/compiler.h>
#include <asm/byteorder.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <asm/bitfield.h>

union ieee754dp {
	struct {
		__BITFIELD_FIELD(unsigned int sign:1,
		__BITFIELD_FIELD(unsigned int bexp:11,
		__BITFIELD_FIELD(u64 mant:52,
		;)))
	};
	u64 bits;
};

union ieee754sp {
	struct {
		__BITFIELD_FIELD(unsigned sign:1,
		__BITFIELD_FIELD(unsigned bexp:8,
		__BITFIELD_FIELD(unsigned mant:23,
		;)))
	};
	u32 bits;
};

/*
 * single precision (often aka float)
*/
int ieee754sp_class(union ieee754sp x);

union ieee754sp ieee754sp_abs(union ieee754sp x);
union ieee754sp ieee754sp_neg(union ieee754sp x);

union ieee754sp ieee754sp_add(union ieee754sp x, union ieee754sp y);
union ieee754sp ieee754sp_sub(union ieee754sp x, union ieee754sp y);
union ieee754sp ieee754sp_mul(union ieee754sp x, union ieee754sp y);
union ieee754sp ieee754sp_div(union ieee754sp x, union ieee754sp y);

union ieee754sp ieee754sp_fint(int x);
union ieee754sp ieee754sp_flong(s64 x);
union ieee754sp ieee754sp_fdp(union ieee754dp x);

int ieee754sp_tint(union ieee754sp x);
s64 ieee754sp_tlong(union ieee754sp x);

int ieee754sp_cmp(union ieee754sp x, union ieee754sp y, int cop, int sig);

union ieee754sp ieee754sp_sqrt(union ieee754sp x);

union ieee754sp ieee754sp_maddf(union ieee754sp z, union ieee754sp x,
				union ieee754sp y);
union ieee754sp ieee754sp_msubf(union ieee754sp z, union ieee754sp x,
				union ieee754sp y);
int ieee754sp_2008class(union ieee754sp x);
union ieee754sp ieee754sp_fmin(union ieee754sp x, union ieee754sp y);
union ieee754sp ieee754sp_fmina(union ieee754sp x, union ieee754sp y);
union ieee754sp ieee754sp_fmax(union ieee754sp x, union ieee754sp y);
union ieee754sp ieee754sp_fmaxa(union ieee754sp x, union ieee754sp y);

/*
 * double precision (often aka double)
*/
int ieee754dp_class(union ieee754dp x);

union ieee754dp ieee754dp_add(union ieee754dp x, union ieee754dp y);
union ieee754dp ieee754dp_sub(union ieee754dp x, union ieee754dp y);
union ieee754dp ieee754dp_mul(union ieee754dp x, union ieee754dp y);
union ieee754dp ieee754dp_div(union ieee754dp x, union ieee754dp y);

union ieee754dp ieee754dp_abs(union ieee754dp x);
union ieee754dp ieee754dp_neg(union ieee754dp x);

union ieee754dp ieee754dp_fint(int x);
union ieee754dp ieee754dp_flong(s64 x);
union ieee754dp ieee754dp_fsp(union ieee754sp x);

int ieee754dp_tint(union ieee754dp x);
s64 ieee754dp_tlong(union ieee754dp x);

int ieee754dp_cmp(union ieee754dp x, union ieee754dp y, int cop, int sig);

union ieee754dp ieee754dp_sqrt(union ieee754dp x);

union ieee754dp ieee754dp_maddf(union ieee754dp z, union ieee754dp x,
				union ieee754dp y);
union ieee754dp ieee754dp_msubf(union ieee754dp z, union ieee754dp x,
				union ieee754dp y);
int ieee754dp_2008class(union ieee754dp x);
union ieee754dp ieee754dp_fmin(union ieee754dp x, union ieee754dp y);
union ieee754dp ieee754dp_fmina(union ieee754dp x, union ieee754dp y);
union ieee754dp ieee754dp_fmax(union ieee754dp x, union ieee754dp y);
union ieee754dp ieee754dp_fmaxa(union ieee754dp x, union ieee754dp y);


/* 5 types of floating point number
*/
enum {
	IEEE754_CLASS_NORM	= 0x00,
	IEEE754_CLASS_ZERO	= 0x01,
	IEEE754_CLASS_DNORM	= 0x02,
	IEEE754_CLASS_INF	= 0x03,
	IEEE754_CLASS_SNAN	= 0x04,
	IEEE754_CLASS_QNAN	= 0x05,
};

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

/*
 * The control status register
 */
struct _ieee754_csr {
	__BITFIELD_FIELD(unsigned fcc:7,	/* condition[7:1] */
	__BITFIELD_FIELD(unsigned nod:1,	/* set 1 for no denormals */
	__BITFIELD_FIELD(unsigned c:1,		/* condition[0] */
	__BITFIELD_FIELD(unsigned pad0:3,
	__BITFIELD_FIELD(unsigned abs2008:1,	/* IEEE 754-2008 ABS/NEG.fmt */
	__BITFIELD_FIELD(unsigned nan2008:1,	/* IEEE 754-2008 NaN mode */
	__BITFIELD_FIELD(unsigned cx:6,		/* exceptions this operation */
	__BITFIELD_FIELD(unsigned mx:5,		/* exception enable  mask */
	__BITFIELD_FIELD(unsigned sx:5,		/* exceptions total */
	__BITFIELD_FIELD(unsigned rm:2,		/* current rounding mode */
	;))))))))))
};
#define ieee754_csr (*(struct _ieee754_csr *)(&current->thread.fpu.fcr31))

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
union ieee754sp ieee754sp_dump(char *s, union ieee754sp x);
union ieee754dp ieee754dp_dump(char *s, union ieee754dp x);

#define IEEE754_SPCVAL_PZERO		0	/* +0.0 */
#define IEEE754_SPCVAL_NZERO		1	/* -0.0 */
#define IEEE754_SPCVAL_PONE		2	/* +1.0 */
#define IEEE754_SPCVAL_NONE		3	/* -1.0 */
#define IEEE754_SPCVAL_PTEN		4	/* +10.0 */
#define IEEE754_SPCVAL_NTEN		5	/* -10.0 */
#define IEEE754_SPCVAL_PINFINITY	6	/* +inf */
#define IEEE754_SPCVAL_NINFINITY	7	/* -inf */
#define IEEE754_SPCVAL_INDEF		8	/* quiet NaN */
#define IEEE754_SPCVAL_PMAX		9	/* +max norm */
#define IEEE754_SPCVAL_NMAX		10	/* -max norm */
#define IEEE754_SPCVAL_PMIN		11	/* +min norm */
#define IEEE754_SPCVAL_NMIN		12	/* -min norm */
#define IEEE754_SPCVAL_PMIND		13	/* +min denorm */
#define IEEE754_SPCVAL_NMIND		14	/* -min denorm */
#define IEEE754_SPCVAL_P1E31		15	/* + 1.0e31 */
#define IEEE754_SPCVAL_P1E63		16	/* + 1.0e63 */

extern const union ieee754dp __ieee754dp_spcvals[];
extern const union ieee754sp __ieee754sp_spcvals[];
#define ieee754dp_spcvals ((const union ieee754dp *)__ieee754dp_spcvals)
#define ieee754sp_spcvals ((const union ieee754sp *)__ieee754sp_spcvals)

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
static inline int ieee754si_indef(void)
{
	return INT_MAX;
}

static inline s64 ieee754di_indef(void)
{
	return S64_MAX;
}

/* result types for xctx.rt */
#define IEEE754_RT_SP	0
#define IEEE754_RT_DP	1
#define IEEE754_RT_XP	2
#define IEEE754_RT_SI	3
#define IEEE754_RT_DI	4

/* compat */
#define ieee754dp_fix(x)	ieee754dp_tint(x)
#define ieee754sp_fix(x)	ieee754sp_tint(x)

#endif /* __ARCH_MIPS_MATH_EMU_IEEE754_H */
