/* SPDX-License-Identifier: GPL-2.0 */
/*
 * McKinley-optimized version of copy_page().
 *
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger <davidm@hpl.hp.com>
 *
 * Inputs:
 *	in0:	address of target page
 *	in1:	address of source page
 * Output:
 *	no return value
 *
 * General idea:
 *	- use regular loads and stores to prefetch data to avoid consuming M-slot just for
 *	  lfetches => good for in-cache performance
 *	- avoid l2 bank-conflicts by not storing into the same 16-byte bank within a single
 *	  cycle
 *
 * Principle of operation:
 *	First, note that L1 has a line-size of 64 bytes and L2 a line-size of 128 bytes.
 *	To avoid secondary misses in L2, we prefetch both source and destination with a line-size
 *	of 128 bytes.  When both of these lines are in the L2 and the first half of the
 *	source line is in L1, we start copying the remaining words.  The second half of the
 *	source line is prefetched in an earlier iteration, so that by the time we start
 *	accessing it, it's also present in the L1.
 *
 *	We use a software-pipelined loop to control the overall operation.  The pipeline
 *	has 2*PREFETCH_DIST+K stages.  The first PREFETCH_DIST stages are used for prefetching
 *	source cache-lines.  The second PREFETCH_DIST stages are used for prefetching destination
 *	cache-lines, the last K stages are used to copy the cache-line words not copied by
 *	the prefetches.  The four relevant points in the pipelined are called A, B, C, D:
 *	p[A] is TRUE if a source-line should be prefetched, p[B] is TRUE if a destination-line
 *	should be prefetched, p[C] is TRUE if the second half of an L2 line should be brought
 *	into L1D and p[D] is TRUE if a cacheline needs to be copied.
 *
 *	This all sounds very complicated, but thanks to the modulo-scheduled loop support,
 *	the resulting code is very regular and quite easy to follow (once you get the idea).
 *
 *	As a secondary optimization, the first 2*PREFETCH_DIST iterations are implemented
 *	as the separate .prefetch_loop.  Logically, this loop performs exactly like the
 *	main-loop (.line_copy), but has all known-to-be-predicated-off instructions removed,
 *	so that each loop iteration is faster (again, good for cached case).
 *
 *	When reading the code, it helps to keep the following picture in mind:
 *
 *	       word 0 word 1
 *            +------+------+---
 *	      |	v[x] | 	t1  | ^
 *	      |	t2   |	t3  | |
 *	      |	t4   |	t5  | |
 *	      |	t6   |	t7  | | 128 bytes
 *     	      |	n[y] | 	t9  | |	(L2 cache line)
 *	      |	t10  | 	t11 | |
 *	      |	t12  | 	t13 | |
 *	      |	t14  | 	t15 | v
 *	      +------+------+---
 *
 *	Here, v[x] is copied by the (memory) prefetch.  n[y] is loaded at p[C]
 *	to fetch the second-half of the L2 cache line into L1, and the tX words are copied in
 *	an order that avoids bank conflicts.
 */
#include <asm/asmmacro.h>
#include <asm/page.h>
#include <asm/export.h>

#define PREFETCH_DIST	8		// McKinley sustains 16 outstanding L2 misses (8 ld, 8 st)

#define src0		r2
#define src1		r3
#define dst0		r9
#define dst1		r10
#define src_pre_mem	r11
#define dst_pre_mem	r14
#define src_pre_l2	r15
#define dst_pre_l2	r16
#define t1		r17
#define t2		r18
#define t3		r19
#define t4		r20
#define t5		t1	// alias!
#define t6		t2	// alias!
#define t7		t3	// alias!
#define t9		t5	// alias!
#define t10		t4	// alias!
#define t11		t7	// alias!
#define t12		t6	// alias!
#define t14		t10	// alias!
#define t13		r21
#define t15		r22

#define saved_lc	r23
#define saved_pr	r24

#define	A	0
#define B	(PREFETCH_DIST)
#define C	(B + PREFETCH_DIST)
#define D	(C + 3)
#define N	(D + 1)
#define Nrot	((N + 7) & ~7)

GLOBAL_ENTRY(copy_page)
	.prologue
	alloc r8 = ar.pfs, 2, Nrot-2, 0, Nrot

	.rotr v[2*PREFETCH_DIST], n[D-C+1]
	.rotp p[N]

	.save ar.lc, saved_lc
	mov saved_lc = ar.lc
	.save pr, saved_pr
	mov saved_pr = pr
	.body

	mov src_pre_mem = in1
	mov pr.rot = 0x10000
	mov ar.ec = 1				// special unrolled loop

	mov dst_pre_mem = in0
	mov ar.lc = 2*PREFETCH_DIST - 1

	add src_pre_l2 = 8*8, in1
	add dst_pre_l2 = 8*8, in0
	add src0 = 8, in1			// first t1 src
	add src1 = 3*8, in1			// first t3 src
	add dst0 = 8, in0			// first t1 dst
	add dst1 = 3*8, in0			// first t3 dst
	mov t1 = (PAGE_SIZE/128) - (2*PREFETCH_DIST) - 1
	nop.m 0
	nop.i 0
	;;
	// same as .line_copy loop, but with all predicated-off instructions removed:
.prefetch_loop:
(p[A])	ld8 v[A] = [src_pre_mem], 128		// M0
(p[B])	st8 [dst_pre_mem] = v[B], 128		// M2
	br.ctop.sptk .prefetch_loop
	;;
	cmp.eq p16, p0 = r0, r0			// reset p16 to 1 (br.ctop cleared it to zero)
	mov ar.lc = t1				// with 64KB pages, t1 is too big to fit in 8 bits!
	mov ar.ec = N				// # of stages in pipeline
	;;
.line_copy:
(p[D])	ld8 t2 = [src0], 3*8			// M0
(p[D])	ld8 t4 = [src1], 3*8			// M1
(p[B])	st8 [dst_pre_mem] = v[B], 128		// M2 prefetch dst from memory
(p[D])	st8 [dst_pre_l2] = n[D-C], 128		// M3 prefetch dst from L2
	;;
(p[A])	ld8 v[A] = [src_pre_mem], 128		// M0 prefetch src from memory
(p[C])	ld8 n[0] = [src_pre_l2], 128		// M1 prefetch src from L2
(p[D])	st8 [dst0] =  t1, 8			// M2
(p[D])	st8 [dst1] =  t3, 8			// M3
	;;
(p[D])	ld8  t5 = [src0], 8
(p[D])	ld8  t7 = [src1], 3*8
(p[D])	st8 [dst0] =  t2, 3*8
(p[D])	st8 [dst1] =  t4, 3*8
	;;
(p[D])	ld8  t6 = [src0], 3*8
(p[D])	ld8 t10 = [src1], 8
(p[D])	st8 [dst0] =  t5, 8
(p[D])	st8 [dst1] =  t7, 3*8
	;;
(p[D])	ld8  t9 = [src0], 3*8
(p[D])	ld8 t11 = [src1], 3*8
(p[D])	st8 [dst0] =  t6, 3*8
(p[D])	st8 [dst1] = t10, 8
	;;
(p[D])	ld8 t12 = [src0], 8
(p[D])	ld8 t14 = [src1], 8
(p[D])	st8 [dst0] =  t9, 3*8
(p[D])	st8 [dst1] = t11, 3*8
	;;
(p[D])	ld8 t13 = [src0], 4*8
(p[D])	ld8 t15 = [src1], 4*8
(p[D])	st8 [dst0] = t12, 8
(p[D])	st8 [dst1] = t14, 8
	;;
(p[D-1])ld8  t1 = [src0], 8
(p[D-1])ld8  t3 = [src1], 8
(p[D])	st8 [dst0] = t13, 4*8
(p[D])	st8 [dst1] = t15, 4*8
	br.ctop.sptk .line_copy
	;;
	mov ar.lc = saved_lc
	mov pr = saved_pr, -1
	br.ret.sptk.many rp
END(copy_page)
EXPORT_SYMBOL(copy_page)
