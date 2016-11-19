/*
 *
 * Optimized version of the standard copy_page() function
 *
 * Inputs:
 *	in0:	address of target page
 *	in1:	address of source page
 * Output:
 *	no return value
 *
 * Copyright (C) 1999, 2001 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger <davidm@hpl.hp.com>
 *
 * 4/06/01 davidm	Tuned to make it perform well both for cached and uncached copies.
 */
#include <asm/asmmacro.h>
#include <asm/page.h>
#include <asm/export.h>

#define PIPE_DEPTH	3
#define EPI		p[PIPE_DEPTH-1]

#define lcount		r16
#define saved_pr	r17
#define saved_lc	r18
#define saved_pfs	r19
#define src1		r20
#define src2		r21
#define tgt1		r22
#define tgt2		r23
#define srcf		r24
#define tgtf		r25
#define tgt_last	r26

#define Nrot		((8*PIPE_DEPTH+7)&~7)

GLOBAL_ENTRY(copy_page)
	.prologue
	.save ar.pfs, saved_pfs
	alloc saved_pfs=ar.pfs,3,Nrot-3,0,Nrot

	.rotr t1[PIPE_DEPTH], t2[PIPE_DEPTH], t3[PIPE_DEPTH], t4[PIPE_DEPTH], \
	      t5[PIPE_DEPTH], t6[PIPE_DEPTH], t7[PIPE_DEPTH], t8[PIPE_DEPTH]
	.rotp p[PIPE_DEPTH]

	.save ar.lc, saved_lc
	mov saved_lc=ar.lc
	mov ar.ec=PIPE_DEPTH

	mov lcount=PAGE_SIZE/64-1
	.save pr, saved_pr
	mov saved_pr=pr
	mov pr.rot=1<<16

	.body

	mov src1=in1
	adds src2=8,in1
	mov tgt_last = PAGE_SIZE
	;;
	adds tgt2=8,in0
	add srcf=512,in1
	mov ar.lc=lcount
	mov tgt1=in0
	add tgtf=512,in0
	add tgt_last = tgt_last, in0
	;;
1:
(p[0])	ld8 t1[0]=[src1],16
(EPI)	st8 [tgt1]=t1[PIPE_DEPTH-1],16
(p[0])	ld8 t2[0]=[src2],16
(EPI)	st8 [tgt2]=t2[PIPE_DEPTH-1],16
	cmp.ltu p6,p0 = tgtf, tgt_last
	;;
(p[0])	ld8 t3[0]=[src1],16
(EPI)	st8 [tgt1]=t3[PIPE_DEPTH-1],16
(p[0])	ld8 t4[0]=[src2],16
(EPI)	st8 [tgt2]=t4[PIPE_DEPTH-1],16
	;;
(p[0])	ld8 t5[0]=[src1],16
(EPI)	st8 [tgt1]=t5[PIPE_DEPTH-1],16
(p[0])	ld8 t6[0]=[src2],16
(EPI)	st8 [tgt2]=t6[PIPE_DEPTH-1],16
	;;
(p[0])	ld8 t7[0]=[src1],16
(EPI)	st8 [tgt1]=t7[PIPE_DEPTH-1],16
(p[0])	ld8 t8[0]=[src2],16
(EPI)	st8 [tgt2]=t8[PIPE_DEPTH-1],16

(p6)	lfetch [srcf], 64
(p6)	lfetch [tgtf], 64
	br.ctop.sptk.few 1b
	;;
	mov pr=saved_pr,0xffffffffffff0000	// restore predicates
	mov ar.pfs=saved_pfs
	mov ar.lc=saved_lc
	br.ret.sptk.many rp
END(copy_page)
EXPORT_SYMBOL(copy_page)
