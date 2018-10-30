/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fast, simple, yet decent quality random number generator based on
 * a paper by David G. Carta ("Two Fast Implementations of the
 * `Minimal Standard' Random Number Generator," Communications of the
 * ACM, January, 1990).
 *
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <asm/asmmacro.h>

#define a	r2
#define m	r3
#define lo	r8
#define hi	r9
#define t0	r16
#define t1	r17
#define	seed	r32

GLOBAL_ENTRY(carta_random32)
	movl	a = (16807 << 16) | 16807
	;;
	pmpyshr2.u t0 = a, seed, 0
	pmpyshr2.u t1 = a, seed, 16
	;;
	unpack2.l t0 = t1, t0
	dep	m = -1, r0, 0, 31
	;;
	zxt4	lo = t0
	shr.u	hi = t0, 32
	;;
	dep	t0 = 0, hi, 15, 49	// t0 = (hi & 0x7fff)
	;;
	shl	t0 = t0, 16		// t0 = (hi & 0x7fff) << 16
	shr	t1 = hi, 15		// t1 = (hi >> 15)
	;;
	add	lo = lo, t0
	;;
	cmp.gtu	p6, p0 = lo, m
	;;
(p6)	and	lo = lo, m
	;;
(p6)	add	lo = 1, lo
	;;
	add	lo = lo, t1
	;;
	cmp.gtu p6, p0 = lo, m
	;;
(p6)	and	lo = lo, m
	;;
(p6)	add	lo = 1, lo
	br.ret.sptk.many rp
END(carta_random32)
