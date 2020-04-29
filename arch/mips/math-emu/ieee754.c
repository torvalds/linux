// SPDX-License-Identifier: GPL-2.0-only
/* ieee754 floating point arithmetic
 * single and double precision
 *
 * BUGS
 * not much dp done
 * doesn't generate IEEE754_INEXACT
 */
/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 */

#include <linux/compiler.h>

#include "ieee754.h"
#include "ieee754sp.h"
#include "ieee754dp.h"

/*
 * Special constants
 */

/*
 * Older GCC requires the inner braces for initialization of union ieee754dp's
 * anonymous struct member.  Without an error will result.
 */
#define xPCNST(s, b, m, ebias)						\
{									\
	{								\
		.sign	= (s),						\
		.bexp	= (b) + ebias,					\
		.mant	= (m)						\
	}								\
}

#define DPCNST(s, b, m)							\
	xPCNST(s, b, m, DP_EBIAS)

const union ieee754dp __ieee754dp_spcvals[] = {
	DPCNST(0, DP_EMIN - 1, 0x0000000000000ULL),	/* + zero   */
	DPCNST(1, DP_EMIN - 1, 0x0000000000000ULL),	/* - zero   */
	DPCNST(0, 0,	       0x0000000000000ULL),	/* + 1.0   */
	DPCNST(1, 0,	       0x0000000000000ULL),	/* - 1.0   */
	DPCNST(0, 3,           0x4000000000000ULL),	/* + 10.0   */
	DPCNST(1, 3,           0x4000000000000ULL),	/* - 10.0   */
	DPCNST(0, DP_EMAX + 1, 0x0000000000000ULL),	/* + infinity */
	DPCNST(1, DP_EMAX + 1, 0x0000000000000ULL),	/* - infinity */
	DPCNST(0, DP_EMAX + 1, 0x7FFFFFFFFFFFFULL),	/* + ind legacy qNaN */
	DPCNST(0, DP_EMAX + 1, 0x8000000000000ULL),	/* + indef 2008 qNaN */
	DPCNST(0, DP_EMAX,     0xFFFFFFFFFFFFFULL),	/* + max */
	DPCNST(1, DP_EMAX,     0xFFFFFFFFFFFFFULL),	/* - max */
	DPCNST(0, DP_EMIN,     0x0000000000000ULL),	/* + min normal */
	DPCNST(1, DP_EMIN,     0x0000000000000ULL),	/* - min normal */
	DPCNST(0, DP_EMIN - 1, 0x0000000000001ULL),	/* + min denormal */
	DPCNST(1, DP_EMIN - 1, 0x0000000000001ULL),	/* - min denormal */
	DPCNST(0, 31,          0x0000000000000ULL),	/* + 1.0e31 */
	DPCNST(0, 63,          0x0000000000000ULL),	/* + 1.0e63 */
};

#define SPCNST(s, b, m)							\
	xPCNST(s, b, m, SP_EBIAS)

const union ieee754sp __ieee754sp_spcvals[] = {
	SPCNST(0, SP_EMIN - 1, 0x000000),	/* + zero   */
	SPCNST(1, SP_EMIN - 1, 0x000000),	/* - zero   */
	SPCNST(0, 0,	       0x000000),	/* + 1.0   */
	SPCNST(1, 0,	       0x000000),	/* - 1.0   */
	SPCNST(0, 3,	       0x200000),	/* + 10.0   */
	SPCNST(1, 3,	       0x200000),	/* - 10.0   */
	SPCNST(0, SP_EMAX + 1, 0x000000),	/* + infinity */
	SPCNST(1, SP_EMAX + 1, 0x000000),	/* - infinity */
	SPCNST(0, SP_EMAX + 1, 0x3FFFFF),	/* + indef legacy quiet NaN */
	SPCNST(0, SP_EMAX + 1, 0x400000),	/* + indef 2008 quiet NaN */
	SPCNST(0, SP_EMAX,     0x7FFFFF),	/* + max normal */
	SPCNST(1, SP_EMAX,     0x7FFFFF),	/* - max normal */
	SPCNST(0, SP_EMIN,     0x000000),	/* + min normal */
	SPCNST(1, SP_EMIN,     0x000000),	/* - min normal */
	SPCNST(0, SP_EMIN - 1, 0x000001),	/* + min denormal */
	SPCNST(1, SP_EMIN - 1, 0x000001),	/* - min denormal */
	SPCNST(0, 31,	       0x000000),	/* + 1.0e31 */
	SPCNST(0, 63,	       0x000000),	/* + 1.0e63 */
};
