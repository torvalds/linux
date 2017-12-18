/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Itanium 2-optimized version of memcpy and copy_user function
 *
 * Inputs:
 * 	in0:	destination address
 *	in1:	source address
 *	in2:	number of bytes to copy
 * Output:
 *	for memcpy:    return dest
 * 	for copy_user: return 0 if success,
 *		       or number of byte NOT copied if error occurred.
 *
 * Copyright (C) 2002 Intel Corp.
 * Copyright (C) 2002 Ken Chen <kenneth.w.chen@intel.com>
 */
#include <asm/asmmacro.h>
#include <asm/page.h>
#include <asm/export.h>

#define EK(y...) EX(y)

/* McKinley specific optimization */

#define retval		r8
#define saved_pfs	r31
#define saved_lc	r10
#define saved_pr	r11
#define saved_in0	r14
#define saved_in1	r15
#define saved_in2	r16

#define src0		r2
#define src1		r3
#define dst0		r17
#define dst1		r18
#define cnt		r9

/* r19-r30 are temp for each code section */
#define PREFETCH_DIST	8
#define src_pre_mem	r19
#define dst_pre_mem	r20
#define src_pre_l2	r21
#define dst_pre_l2	r22
#define t1		r23
#define t2		r24
#define t3		r25
#define t4		r26
#define t5		t1	// alias!
#define t6		t2	// alias!
#define t7		t3	// alias!
#define n8		r27
#define t9		t5	// alias!
#define t10		t4	// alias!
#define t11		t7	// alias!
#define t12		t6	// alias!
#define t14		t10	// alias!
#define t13		r28
#define t15		r29
#define tmp		r30

/* defines for long_copy block */
#define	A	0
#define B	(PREFETCH_DIST)
#define C	(B + PREFETCH_DIST)
#define D	(C + 1)
#define N	(D + 1)
#define Nrot	((N + 7) & ~7)

/* alias */
#define in0		r32
#define in1		r33
#define in2		r34

GLOBAL_ENTRY(memcpy)
	and	r28=0x7,in0
	and	r29=0x7,in1
	mov	f6=f0
	mov	retval=in0
	br.cond.sptk .common_code
	;;
END(memcpy)
EXPORT_SYMBOL(memcpy)
GLOBAL_ENTRY(__copy_user)
	.prologue
// check dest alignment
	and	r28=0x7,in0
	and	r29=0x7,in1
	mov	f6=f1
	mov	saved_in0=in0	// save dest pointer
	mov	saved_in1=in1	// save src pointer
	mov	retval=r0	// initialize return value
	;;
.common_code:
	cmp.gt	p15,p0=8,in2	// check for small size
	cmp.ne	p13,p0=0,r28	// check dest alignment
	cmp.ne	p14,p0=0,r29	// check src alignment
	add	src0=0,in1
	sub	r30=8,r28	// for .align_dest
	mov	saved_in2=in2	// save len
	;;
	add	dst0=0,in0
	add	dst1=1,in0	// dest odd index
	cmp.le	p6,p0 = 1,r30	// for .align_dest
(p15)	br.cond.dpnt .memcpy_short
(p13)	br.cond.dpnt .align_dest
(p14)	br.cond.dpnt .unaligned_src
	;;

// both dest and src are aligned on 8-byte boundary
.aligned_src:
	.save ar.pfs, saved_pfs
	alloc	saved_pfs=ar.pfs,3,Nrot-3,0,Nrot
	.save pr, saved_pr
	mov	saved_pr=pr

	shr.u	cnt=in2,7	// this much cache line
	;;
	cmp.lt	p6,p0=2*PREFETCH_DIST,cnt
	cmp.lt	p7,p8=1,cnt
	.save ar.lc, saved_lc
	mov	saved_lc=ar.lc
	.body
	add	cnt=-1,cnt
	add	src_pre_mem=0,in1	// prefetch src pointer
	add	dst_pre_mem=0,in0	// prefetch dest pointer
	;;
(p7)	mov	ar.lc=cnt	// prefetch count
(p8)	mov	ar.lc=r0
(p6)	br.cond.dpnt .long_copy
	;;

.prefetch:
	lfetch.fault	  [src_pre_mem], 128
	lfetch.fault.excl [dst_pre_mem], 128
	br.cloop.dptk.few .prefetch
	;;

.medium_copy:
	and	tmp=31,in2	// copy length after iteration
	shr.u	r29=in2,5	// number of 32-byte iteration
	add	dst1=8,dst0	// 2nd dest pointer
	;;
	add	cnt=-1,r29	// ctop iteration adjustment
	cmp.eq	p10,p0=r29,r0	// do we really need to loop?
	add	src1=8,src0	// 2nd src pointer
	cmp.le	p6,p0=8,tmp
	;;
	cmp.le	p7,p0=16,tmp
	mov	ar.lc=cnt	// loop setup
	cmp.eq	p16,p17 = r0,r0
	mov	ar.ec=2
(p10)	br.dpnt.few .aligned_src_tail
	;;
	TEXT_ALIGN(32)
1:
EX(.ex_handler, (p16)	ld8	r34=[src0],16)
EK(.ex_handler, (p16)	ld8	r38=[src1],16)
EX(.ex_handler, (p17)	st8	[dst0]=r33,16)
EK(.ex_handler, (p17)	st8	[dst1]=r37,16)
	;;
EX(.ex_handler, (p16)	ld8	r32=[src0],16)
EK(.ex_handler, (p16)	ld8	r36=[src1],16)
EX(.ex_handler, (p16)	st8	[dst0]=r34,16)
EK(.ex_handler, (p16)	st8	[dst1]=r38,16)
	br.ctop.dptk.few 1b
	;;

.aligned_src_tail:
EX(.ex_handler, (p6)	ld8	t1=[src0])
	mov	ar.lc=saved_lc
	mov	ar.pfs=saved_pfs
EX(.ex_hndlr_s, (p7)	ld8	t2=[src1],8)
	cmp.le	p8,p0=24,tmp
	and	r21=-8,tmp
	;;
EX(.ex_hndlr_s, (p8)	ld8	t3=[src1])
EX(.ex_handler, (p6)	st8	[dst0]=t1)	// store byte 1
	and	in2=7,tmp	// remaining length
EX(.ex_hndlr_d, (p7)	st8	[dst1]=t2,8)	// store byte 2
	add	src0=src0,r21	// setting up src pointer
	add	dst0=dst0,r21	// setting up dest pointer
	;;
EX(.ex_handler, (p8)	st8	[dst1]=t3)	// store byte 3
	mov	pr=saved_pr,-1
	br.dptk.many .memcpy_short
	;;

/* code taken from copy_page_mck */
.long_copy:
	.rotr v[2*PREFETCH_DIST]
	.rotp p[N]

	mov src_pre_mem = src0
	mov pr.rot = 0x10000
	mov ar.ec = 1				// special unrolled loop

	mov dst_pre_mem = dst0

	add src_pre_l2 = 8*8, src0
	add dst_pre_l2 = 8*8, dst0
	;;
	add src0 = 8, src_pre_mem		// first t1 src
	mov ar.lc = 2*PREFETCH_DIST - 1
	shr.u cnt=in2,7				// number of lines
	add src1 = 3*8, src_pre_mem		// first t3 src
	add dst0 = 8, dst_pre_mem		// first t1 dst
	add dst1 = 3*8, dst_pre_mem		// first t3 dst
	;;
	and tmp=127,in2				// remaining bytes after this block
	add cnt = -(2*PREFETCH_DIST) - 1, cnt
	// same as .line_copy loop, but with all predicated-off instructions removed:
.prefetch_loop:
EX(.ex_hndlr_lcpy_1, (p[A])	ld8 v[A] = [src_pre_mem], 128)		// M0
EK(.ex_hndlr_lcpy_1, (p[B])	st8 [dst_pre_mem] = v[B], 128)		// M2
	br.ctop.sptk .prefetch_loop
	;;
	cmp.eq p16, p0 = r0, r0			// reset p16 to 1
	mov ar.lc = cnt
	mov ar.ec = N				// # of stages in pipeline
	;;
.line_copy:
EX(.ex_handler,	(p[D])	ld8 t2 = [src0], 3*8)			// M0
EK(.ex_handler,	(p[D])	ld8 t4 = [src1], 3*8)			// M1
EX(.ex_handler_lcpy,	(p[B])	st8 [dst_pre_mem] = v[B], 128)		// M2 prefetch dst from memory
EK(.ex_handler_lcpy,	(p[D])	st8 [dst_pre_l2] = n8, 128)		// M3 prefetch dst from L2
	;;
EX(.ex_handler_lcpy,	(p[A])	ld8 v[A] = [src_pre_mem], 128)		// M0 prefetch src from memory
EK(.ex_handler_lcpy,	(p[C])	ld8 n8 = [src_pre_l2], 128)		// M1 prefetch src from L2
EX(.ex_handler,	(p[D])	st8 [dst0] =  t1, 8)			// M2
EK(.ex_handler,	(p[D])	st8 [dst1] =  t3, 8)			// M3
	;;
EX(.ex_handler,	(p[D])	ld8  t5 = [src0], 8)
EK(.ex_handler,	(p[D])	ld8  t7 = [src1], 3*8)
EX(.ex_handler,	(p[D])	st8 [dst0] =  t2, 3*8)
EK(.ex_handler,	(p[D])	st8 [dst1] =  t4, 3*8)
	;;
EX(.ex_handler,	(p[D])	ld8  t6 = [src0], 3*8)
EK(.ex_handler,	(p[D])	ld8 t10 = [src1], 8)
EX(.ex_handler,	(p[D])	st8 [dst0] =  t5, 8)
EK(.ex_handler,	(p[D])	st8 [dst1] =  t7, 3*8)
	;;
EX(.ex_handler,	(p[D])	ld8  t9 = [src0], 3*8)
EK(.ex_handler,	(p[D])	ld8 t11 = [src1], 3*8)
EX(.ex_handler,	(p[D])	st8 [dst0] =  t6, 3*8)
EK(.ex_handler,	(p[D])	st8 [dst1] = t10, 8)
	;;
EX(.ex_handler,	(p[D])	ld8 t12 = [src0], 8)
EK(.ex_handler,	(p[D])	ld8 t14 = [src1], 8)
EX(.ex_handler,	(p[D])	st8 [dst0] =  t9, 3*8)
EK(.ex_handler,	(p[D])	st8 [dst1] = t11, 3*8)
	;;
EX(.ex_handler,	(p[D])	ld8 t13 = [src0], 4*8)
EK(.ex_handler,	(p[D])	ld8 t15 = [src1], 4*8)
EX(.ex_handler,	(p[D])	st8 [dst0] = t12, 8)
EK(.ex_handler,	(p[D])	st8 [dst1] = t14, 8)
	;;
EX(.ex_handler,	(p[C])	ld8  t1 = [src0], 8)
EK(.ex_handler,	(p[C])	ld8  t3 = [src1], 8)
EX(.ex_handler,	(p[D])	st8 [dst0] = t13, 4*8)
EK(.ex_handler,	(p[D])	st8 [dst1] = t15, 4*8)
	br.ctop.sptk .line_copy
	;;

	add dst0=-8,dst0
	add src0=-8,src0
	mov in2=tmp
	.restore sp
	br.sptk.many .medium_copy
	;;

#define BLOCK_SIZE	128*32
#define blocksize	r23
#define curlen		r24

// dest is on 8-byte boundary, src is not. We need to do
// ld8-ld8, shrp, then st8.  Max 8 byte copy per cycle.
.unaligned_src:
	.prologue
	.save ar.pfs, saved_pfs
	alloc	saved_pfs=ar.pfs,3,5,0,8
	.save ar.lc, saved_lc
	mov	saved_lc=ar.lc
	.save pr, saved_pr
	mov	saved_pr=pr
	.body
.4k_block:
	mov	saved_in0=dst0	// need to save all input arguments
	mov	saved_in2=in2
	mov	blocksize=BLOCK_SIZE
	;;
	cmp.lt	p6,p7=blocksize,in2
	mov	saved_in1=src0
	;;
(p6)	mov	in2=blocksize
	;;
	shr.u	r21=in2,7	// this much cache line
	shr.u	r22=in2,4	// number of 16-byte iteration
	and	curlen=15,in2	// copy length after iteration
	and	r30=7,src0	// source alignment
	;;
	cmp.lt	p7,p8=1,r21
	add	cnt=-1,r21
	;;

	add	src_pre_mem=0,src0	// prefetch src pointer
	add	dst_pre_mem=0,dst0	// prefetch dest pointer
	and	src0=-8,src0		// 1st src pointer
(p7)	mov	ar.lc = cnt
(p8)	mov	ar.lc = r0
	;;
	TEXT_ALIGN(32)
1:	lfetch.fault	  [src_pre_mem], 128
	lfetch.fault.excl [dst_pre_mem], 128
	br.cloop.dptk.few 1b
	;;

	shladd	dst1=r22,3,dst0	// 2nd dest pointer
	shladd	src1=r22,3,src0	// 2nd src pointer
	cmp.eq	p8,p9=r22,r0	// do we really need to loop?
	cmp.le	p6,p7=8,curlen;	// have at least 8 byte remaining?
	add	cnt=-1,r22	// ctop iteration adjustment
	;;
EX(.ex_handler, (p9)	ld8	r33=[src0],8)	// loop primer
EK(.ex_handler, (p9)	ld8	r37=[src1],8)
(p8)	br.dpnt.few .noloop
	;;

// The jump address is calculated based on src alignment. The COPYU
// macro below need to confine its size to power of two, so an entry
// can be caulated using shl instead of an expensive multiply. The
// size is then hard coded by the following #define to match the
// actual size.  This make it somewhat tedious when COPYU macro gets
// changed and this need to be adjusted to match.
#define LOOP_SIZE 6
1:
	mov	r29=ip		// jmp_table thread
	mov	ar.lc=cnt
	;;
	add	r29=.jump_table - 1b - (.jmp1-.jump_table), r29
	shl	r28=r30, LOOP_SIZE	// jmp_table thread
	mov	ar.ec=2		// loop setup
	;;
	add	r29=r29,r28		// jmp_table thread
	cmp.eq	p16,p17=r0,r0
	;;
	mov	b6=r29			// jmp_table thread
	;;
	br.cond.sptk.few b6

// for 8-15 byte case
// We will skip the loop, but need to replicate the side effect
// that the loop produces.
.noloop:
EX(.ex_handler, (p6)	ld8	r37=[src1],8)
	add	src0=8,src0
(p6)	shl	r25=r30,3
	;;
EX(.ex_handler, (p6)	ld8	r27=[src1])
(p6)	shr.u	r28=r37,r25
(p6)	sub	r26=64,r25
	;;
(p6)	shl	r27=r27,r26
	;;
(p6)	or	r21=r28,r27

.unaligned_src_tail:
/* check if we have more than blocksize to copy, if so go back */
	cmp.gt	p8,p0=saved_in2,blocksize
	;;
(p8)	add	dst0=saved_in0,blocksize
(p8)	add	src0=saved_in1,blocksize
(p8)	sub	in2=saved_in2,blocksize
(p8)	br.dpnt	.4k_block
	;;

/* we have up to 15 byte to copy in the tail.
 * part of work is already done in the jump table code
 * we are at the following state.
 * src side:
 * 
 *   xxxxxx xx                   <----- r21 has xxxxxxxx already
 * -------- -------- --------
 * 0        8        16
 *          ^
 *          |
 *          src1
 * 
 * dst
 * -------- -------- --------
 * ^
 * |
 * dst1
 */
EX(.ex_handler, (p6)	st8	[dst1]=r21,8)	// more than 8 byte to copy
(p6)	add	curlen=-8,curlen	// update length
	mov	ar.pfs=saved_pfs
	;;
	mov	ar.lc=saved_lc
	mov	pr=saved_pr,-1
	mov	in2=curlen	// remaining length
	mov	dst0=dst1	// dest pointer
	add	src0=src1,r30	// forward by src alignment
	;;

// 7 byte or smaller.
.memcpy_short:
	cmp.le	p8,p9   = 1,in2
	cmp.le	p10,p11 = 2,in2
	cmp.le	p12,p13 = 3,in2
	cmp.le	p14,p15 = 4,in2
	add	src1=1,src0	// second src pointer
	add	dst1=1,dst0	// second dest pointer
	;;

EX(.ex_handler_short, (p8)	ld1	t1=[src0],2)
EK(.ex_handler_short, (p10)	ld1	t2=[src1],2)
(p9)	br.ret.dpnt rp		// 0 byte copy
	;;

EX(.ex_handler_short, (p8)	st1	[dst0]=t1,2)
EK(.ex_handler_short, (p10)	st1	[dst1]=t2,2)
(p11)	br.ret.dpnt rp		// 1 byte copy

EX(.ex_handler_short, (p12)	ld1	t3=[src0],2)
EK(.ex_handler_short, (p14)	ld1	t4=[src1],2)
(p13)	br.ret.dpnt rp		// 2 byte copy
	;;

	cmp.le	p6,p7   = 5,in2
	cmp.le	p8,p9   = 6,in2
	cmp.le	p10,p11 = 7,in2

EX(.ex_handler_short, (p12)	st1	[dst0]=t3,2)
EK(.ex_handler_short, (p14)	st1	[dst1]=t4,2)
(p15)	br.ret.dpnt rp		// 3 byte copy
	;;

EX(.ex_handler_short, (p6)	ld1	t5=[src0],2)
EK(.ex_handler_short, (p8)	ld1	t6=[src1],2)
(p7)	br.ret.dpnt rp		// 4 byte copy
	;;

EX(.ex_handler_short, (p6)	st1	[dst0]=t5,2)
EK(.ex_handler_short, (p8)	st1	[dst1]=t6,2)
(p9)	br.ret.dptk rp		// 5 byte copy

EX(.ex_handler_short, (p10)	ld1	t7=[src0],2)
(p11)	br.ret.dptk rp		// 6 byte copy
	;;

EX(.ex_handler_short, (p10)	st1	[dst0]=t7,2)
	br.ret.dptk rp		// done all cases


/* Align dest to nearest 8-byte boundary. We know we have at
 * least 7 bytes to copy, enough to crawl to 8-byte boundary.
 * Actual number of byte to crawl depend on the dest alignment.
 * 7 byte or less is taken care at .memcpy_short

 * src0 - source even index
 * src1 - source  odd index
 * dst0 - dest even index
 * dst1 - dest  odd index
 * r30  - distance to 8-byte boundary
 */

.align_dest:
	add	src1=1,in1	// source odd index
	cmp.le	p7,p0 = 2,r30	// for .align_dest
	cmp.le	p8,p0 = 3,r30	// for .align_dest
EX(.ex_handler_short, (p6)	ld1	t1=[src0],2)
	cmp.le	p9,p0 = 4,r30	// for .align_dest
	cmp.le	p10,p0 = 5,r30
	;;
EX(.ex_handler_short, (p7)	ld1	t2=[src1],2)
EK(.ex_handler_short, (p8)	ld1	t3=[src0],2)
	cmp.le	p11,p0 = 6,r30
EX(.ex_handler_short, (p6)	st1	[dst0] = t1,2)
	cmp.le	p12,p0 = 7,r30
	;;
EX(.ex_handler_short, (p9)	ld1	t4=[src1],2)
EK(.ex_handler_short, (p10)	ld1	t5=[src0],2)
EX(.ex_handler_short, (p7)	st1	[dst1] = t2,2)
EK(.ex_handler_short, (p8)	st1	[dst0] = t3,2)
	;;
EX(.ex_handler_short, (p11)	ld1	t6=[src1],2)
EK(.ex_handler_short, (p12)	ld1	t7=[src0],2)
	cmp.eq	p6,p7=r28,r29
EX(.ex_handler_short, (p9)	st1	[dst1] = t4,2)
EK(.ex_handler_short, (p10)	st1	[dst0] = t5,2)
	sub	in2=in2,r30
	;;
EX(.ex_handler_short, (p11)	st1	[dst1] = t6,2)
EK(.ex_handler_short, (p12)	st1	[dst0] = t7)
	add	dst0=in0,r30	// setup arguments
	add	src0=in1,r30
(p6)	br.cond.dptk .aligned_src
(p7)	br.cond.dpnt .unaligned_src
	;;

/* main loop body in jump table format */
#define COPYU(shift)									\
1:											\
EX(.ex_handler,  (p16)	ld8	r32=[src0],8);		/* 1 */				\
EK(.ex_handler,  (p16)	ld8	r36=[src1],8);						\
		 (p17)	shrp	r35=r33,r34,shift;;	/* 1 */				\
EX(.ex_handler,  (p6)	ld8	r22=[src1]);	/* common, prime for tail section */	\
		 nop.m	0;								\
		 (p16)	shrp	r38=r36,r37,shift;					\
EX(.ex_handler,  (p17)	st8	[dst0]=r35,8);		/* 1 */				\
EK(.ex_handler,  (p17)	st8	[dst1]=r39,8);						\
		 br.ctop.dptk.few 1b;;							\
		 (p7)	add	src1=-8,src1;	/* back out for <8 byte case */		\
		 shrp	r21=r22,r38,shift;	/* speculative work */			\
		 br.sptk.few .unaligned_src_tail /* branch out of jump table */		\
		 ;;
	TEXT_ALIGN(32)
.jump_table:
	COPYU(8)	// unaligned cases
.jmp1:
	COPYU(16)
	COPYU(24)
	COPYU(32)
	COPYU(40)
	COPYU(48)
	COPYU(56)

#undef A
#undef B
#undef C
#undef D

/*
 * Due to lack of local tag support in gcc 2.x assembler, it is not clear which
 * instruction failed in the bundle.  The exception algorithm is that we
 * first figure out the faulting address, then detect if there is any
 * progress made on the copy, if so, redo the copy from last known copied
 * location up to the faulting address (exclusive). In the copy_from_user
 * case, remaining byte in kernel buffer will be zeroed.
 *
 * Take copy_from_user as an example, in the code there are multiple loads
 * in a bundle and those multiple loads could span over two pages, the
 * faulting address is calculated as page_round_down(max(src0, src1)).
 * This is based on knowledge that if we can access one byte in a page, we
 * can access any byte in that page.
 *
 * predicate used in the exception handler:
 * p6-p7: direction
 * p10-p11: src faulting addr calculation
 * p12-p13: dst faulting addr calculation
 */

#define A	r19
#define B	r20
#define C	r21
#define D	r22
#define F	r28

#define saved_retval	loc0
#define saved_rtlink	loc1
#define saved_pfs_stack	loc2

.ex_hndlr_s:
	add	src0=8,src0
	br.sptk .ex_handler
	;;
.ex_hndlr_d:
	add	dst0=8,dst0
	br.sptk .ex_handler
	;;
.ex_hndlr_lcpy_1:
	mov	src1=src_pre_mem
	mov	dst1=dst_pre_mem
	cmp.gtu	p10,p11=src_pre_mem,saved_in1
	cmp.gtu	p12,p13=dst_pre_mem,saved_in0
	;;
(p10)	add	src0=8,saved_in1
(p11)	mov	src0=saved_in1
(p12)	add	dst0=8,saved_in0
(p13)	mov	dst0=saved_in0
	br.sptk	.ex_handler
.ex_handler_lcpy:
	// in line_copy block, the preload addresses should always ahead
	// of the other two src/dst pointers.  Furthermore, src1/dst1 should
	// always ahead of src0/dst0.
	mov	src1=src_pre_mem
	mov	dst1=dst_pre_mem
.ex_handler:
	mov	pr=saved_pr,-1		// first restore pr, lc, and pfs
	mov	ar.lc=saved_lc
	mov	ar.pfs=saved_pfs
	;;
.ex_handler_short: // fault occurred in these sections didn't change pr, lc, pfs
	cmp.ltu	p6,p7=saved_in0, saved_in1	// get the copy direction
	cmp.ltu	p10,p11=src0,src1
	cmp.ltu	p12,p13=dst0,dst1
	fcmp.eq	p8,p0=f6,f0		// is it memcpy?
	mov	tmp = dst0
	;;
(p11)	mov	src1 = src0		// pick the larger of the two
(p13)	mov	dst0 = dst1		// make dst0 the smaller one
(p13)	mov	dst1 = tmp		// and dst1 the larger one
	;;
(p6)	dep	F = r0,dst1,0,PAGE_SHIFT // usr dst round down to page boundary
(p7)	dep	F = r0,src1,0,PAGE_SHIFT // usr src round down to page boundary
	;;
(p6)	cmp.le	p14,p0=dst0,saved_in0	// no progress has been made on store
(p7)	cmp.le	p14,p0=src0,saved_in1	// no progress has been made on load
	mov	retval=saved_in2
(p8)	ld1	tmp=[src1]		// force an oops for memcpy call
(p8)	st1	[dst1]=r0		// force an oops for memcpy call
(p14)	br.ret.sptk.many rp

/*
 * The remaining byte to copy is calculated as:
 *
 * A =	(faulting_addr - orig_src)	-> len to faulting ld address
 *	or 
 * 	(faulting_addr - orig_dst)	-> len to faulting st address
 * B =	(cur_dst - orig_dst)		-> len copied so far
 * C =	A - B				-> len need to be copied
 * D =	orig_len - A			-> len need to be left along
 */
(p6)	sub	A = F, saved_in0
(p7)	sub	A = F, saved_in1
	clrrrb
	;;
	alloc	saved_pfs_stack=ar.pfs,3,3,3,0
	cmp.lt	p8,p0=A,r0
	sub	B = dst0, saved_in0	// how many byte copied so far
	;;
(p8)	mov	A = 0;			// A shouldn't be negative, cap it
	;;
	sub	C = A, B
	sub	D = saved_in2, A
	;;
	cmp.gt	p8,p0=C,r0		// more than 1 byte?
	mov	r8=0
	mov	saved_retval = D
	mov	saved_rtlink = b0

	add	out0=saved_in0, B
	add	out1=saved_in1, B
	mov	out2=C
(p8)	br.call.sptk.few b0=__copy_user	// recursive call
	;;

	add	saved_retval=saved_retval,r8	// above might return non-zero value
	;;

	mov	retval=saved_retval
	mov	ar.pfs=saved_pfs_stack
	mov	b0=saved_rtlink
	br.ret.sptk.many rp

/* end of McKinley specific optimization */
END(__copy_user)
EXPORT_SYMBOL(__copy_user)
