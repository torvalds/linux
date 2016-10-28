/*
 *
 * Optimized version of the copy_user() routine.
 * It is used to copy date across the kernel/user boundary.
 *
 * The source and destination are always on opposite side of
 * the boundary. When reading from user space we must catch
 * faults on loads. When writing to user space we must catch
 * errors on stores. Note that because of the nature of the copy
 * we don't need to worry about overlapping regions.
 *
 *
 * Inputs:
 *	in0	address of source buffer
 *	in1	address of destination buffer
 *	in2	number of bytes to copy
 *
 * Outputs:
 *	ret0	0 in case of success. The number of bytes NOT copied in
 *		case of error.
 *
 * Copyright (C) 2000-2001 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * Fixme:
 *	- handle the case where we have more than 16 bytes and the alignment
 *	  are different.
 *	- more benchmarking
 *	- fix extraneous stop bit introduced by the EX() macro.
 */

#include <asm/asmmacro.h>
#include <asm/export.h>

//
// Tuneable parameters
//
#define COPY_BREAK	16	// we do byte copy below (must be >=16)
#define PIPE_DEPTH	21	// pipe depth

#define EPI		p[PIPE_DEPTH-1]

//
// arguments
//
#define dst		in0
#define src		in1
#define len		in2

//
// local registers
//
#define t1		r2	// rshift in bytes
#define t2		r3	// lshift in bytes
#define rshift		r14	// right shift in bits
#define lshift		r15	// left shift in bits
#define word1		r16
#define word2		r17
#define cnt		r18
#define len2		r19
#define saved_lc	r20
#define saved_pr	r21
#define tmp		r22
#define val		r23
#define src1		r24
#define dst1		r25
#define src2		r26
#define dst2		r27
#define len1		r28
#define enddst		r29
#define endsrc		r30
#define saved_pfs	r31

GLOBAL_ENTRY(__copy_user)
	.prologue
	.save ar.pfs, saved_pfs
	alloc saved_pfs=ar.pfs,3,((2*PIPE_DEPTH+7)&~7),0,((2*PIPE_DEPTH+7)&~7)

	.rotr val1[PIPE_DEPTH],val2[PIPE_DEPTH]
	.rotp p[PIPE_DEPTH]

	adds len2=-1,len	// br.ctop is repeat/until
	mov ret0=r0

	;;			// RAW of cfm when len=0
	cmp.eq p8,p0=r0,len	// check for zero length
	.save ar.lc, saved_lc
	mov saved_lc=ar.lc	// preserve ar.lc (slow)
(p8)	br.ret.spnt.many rp	// empty mempcy()
	;;
	add enddst=dst,len	// first byte after end of source
	add endsrc=src,len	// first byte after end of destination
	.save pr, saved_pr
	mov saved_pr=pr		// preserve predicates

	.body

	mov dst1=dst		// copy because of rotation
	mov ar.ec=PIPE_DEPTH
	mov pr.rot=1<<16	// p16=true all others are false

	mov src1=src		// copy because of rotation
	mov ar.lc=len2		// initialize lc for small count
	cmp.lt p10,p7=COPY_BREAK,len	// if len > COPY_BREAK then long copy

	xor tmp=src,dst		// same alignment test prepare
(p10)	br.cond.dptk .long_copy_user
	;;			// RAW pr.rot/p16 ?
	//
	// Now we do the byte by byte loop with software pipeline
	//
	// p7 is necessarily false by now
1:
	EX(.failure_in_pipe1,(p16) ld1 val1[0]=[src1],1)
	EX(.failure_out,(EPI) st1 [dst1]=val1[PIPE_DEPTH-1],1)
	br.ctop.dptk.few 1b
	;;
	mov ar.lc=saved_lc
	mov pr=saved_pr,0xffffffffffff0000
	mov ar.pfs=saved_pfs		// restore ar.ec
	br.ret.sptk.many rp		// end of short memcpy

	//
	// Not 8-byte aligned
	//
.diff_align_copy_user:
	// At this point we know we have more than 16 bytes to copy
	// and also that src and dest do _not_ have the same alignment.
	and src2=0x7,src1				// src offset
	and dst2=0x7,dst1				// dst offset
	;;
	// The basic idea is that we copy byte-by-byte at the head so
	// that we can reach 8-byte alignment for both src1 and dst1.
	// Then copy the body using software pipelined 8-byte copy,
	// shifting the two back-to-back words right and left, then copy
	// the tail by copying byte-by-byte.
	//
	// Fault handling. If the byte-by-byte at the head fails on the
	// load, then restart and finish the pipleline by copying zeros
	// to the dst1. Then copy zeros for the rest of dst1.
	// If 8-byte software pipeline fails on the load, do the same as
	// failure_in3 does. If the byte-by-byte at the tail fails, it is
	// handled simply by failure_in_pipe1.
	//
	// The case p14 represents the source has more bytes in the
	// the first word (by the shifted part), whereas the p15 needs to
	// copy some bytes from the 2nd word of the source that has the
	// tail of the 1st of the destination.
	//

	//
	// Optimization. If dst1 is 8-byte aligned (quite common), we don't need
	// to copy the head to dst1, to start 8-byte copy software pipeline.
	// We know src1 is not 8-byte aligned in this case.
	//
	cmp.eq p14,p15=r0,dst2
(p15)	br.cond.spnt 1f
	;;
	sub t1=8,src2
	mov t2=src2
	;;
	shl rshift=t2,3
	sub len1=len,t1					// set len1
	;;
	sub lshift=64,rshift
	;;
	br.cond.spnt .word_copy_user
	;;
1:
	cmp.leu	p14,p15=src2,dst2
	sub t1=dst2,src2
	;;
	.pred.rel "mutex", p14, p15
(p14)	sub word1=8,src2				// (8 - src offset)
(p15)	sub t1=r0,t1					// absolute value
(p15)	sub word1=8,dst2				// (8 - dst offset)
	;;
	// For the case p14, we don't need to copy the shifted part to
	// the 1st word of destination.
	sub t2=8,t1
(p14)	sub word1=word1,t1
	;;
	sub len1=len,word1				// resulting len
(p15)	shl rshift=t1,3					// in bits
(p14)	shl rshift=t2,3
	;;
(p14)	sub len1=len1,t1
	adds cnt=-1,word1
	;;
	sub lshift=64,rshift
	mov ar.ec=PIPE_DEPTH
	mov pr.rot=1<<16	// p16=true all others are false
	mov ar.lc=cnt
	;;
2:
	EX(.failure_in_pipe2,(p16) ld1 val1[0]=[src1],1)
	EX(.failure_out,(EPI) st1 [dst1]=val1[PIPE_DEPTH-1],1)
	br.ctop.dptk.few 2b
	;;
	clrrrb
	;;
.word_copy_user:
	cmp.gtu p9,p0=16,len1
(p9)	br.cond.spnt 4f			// if (16 > len1) skip 8-byte copy
	;;
	shr.u cnt=len1,3		// number of 64-bit words
	;;
	adds cnt=-1,cnt
	;;
	.pred.rel "mutex", p14, p15
(p14)	sub src1=src1,t2
(p15)	sub src1=src1,t1
	//
	// Now both src1 and dst1 point to an 8-byte aligned address. And
	// we have more than 8 bytes to copy.
	//
	mov ar.lc=cnt
	mov ar.ec=PIPE_DEPTH
	mov pr.rot=1<<16	// p16=true all others are false
	;;
3:
	//
	// The pipleline consists of 3 stages:
	// 1 (p16):	Load a word from src1
	// 2 (EPI_1):	Shift right pair, saving to tmp
	// 3 (EPI):	Store tmp to dst1
	//
	// To make it simple, use at least 2 (p16) loops to set up val1[n]
	// because we need 2 back-to-back val1[] to get tmp.
	// Note that this implies EPI_2 must be p18 or greater.
	//

#define EPI_1		p[PIPE_DEPTH-2]
#define SWITCH(pred, shift)	cmp.eq pred,p0=shift,rshift
#define CASE(pred, shift)	\
	(pred)	br.cond.spnt .copy_user_bit##shift
#define BODY(rshift)						\
.copy_user_bit##rshift:						\
1:								\
	EX(.failure_out,(EPI) st8 [dst1]=tmp,8);		\
(EPI_1) shrp tmp=val1[PIPE_DEPTH-2],val1[PIPE_DEPTH-1],rshift;	\
	EX(3f,(p16) ld8 val1[1]=[src1],8);			\
(p16)	mov val1[0]=r0;						\
	br.ctop.dptk 1b;					\
	;;							\
	br.cond.sptk.many .diff_align_do_tail;			\
2:								\
(EPI)	st8 [dst1]=tmp,8;					\
(EPI_1)	shrp tmp=val1[PIPE_DEPTH-2],val1[PIPE_DEPTH-1],rshift;	\
3:								\
(p16)	mov val1[1]=r0;						\
(p16)	mov val1[0]=r0;						\
	br.ctop.dptk 2b;					\
	;;							\
	br.cond.sptk.many .failure_in2

	//
	// Since the instruction 'shrp' requires a fixed 128-bit value
	// specifying the bits to shift, we need to provide 7 cases
	// below.
	//
	SWITCH(p6, 8)
	SWITCH(p7, 16)
	SWITCH(p8, 24)
	SWITCH(p9, 32)
	SWITCH(p10, 40)
	SWITCH(p11, 48)
	SWITCH(p12, 56)
	;;
	CASE(p6, 8)
	CASE(p7, 16)
	CASE(p8, 24)
	CASE(p9, 32)
	CASE(p10, 40)
	CASE(p11, 48)
	CASE(p12, 56)
	;;
	BODY(8)
	BODY(16)
	BODY(24)
	BODY(32)
	BODY(40)
	BODY(48)
	BODY(56)
	;;
.diff_align_do_tail:
	.pred.rel "mutex", p14, p15
(p14)	sub src1=src1,t1
(p14)	adds dst1=-8,dst1
(p15)	sub dst1=dst1,t1
	;;
4:
	// Tail correction.
	//
	// The problem with this piplelined loop is that the last word is not
	// loaded and thus parf of the last word written is not correct.
	// To fix that, we simply copy the tail byte by byte.

	sub len1=endsrc,src1,1
	clrrrb
	;;
	mov ar.ec=PIPE_DEPTH
	mov pr.rot=1<<16	// p16=true all others are false
	mov ar.lc=len1
	;;
5:
	EX(.failure_in_pipe1,(p16) ld1 val1[0]=[src1],1)
	EX(.failure_out,(EPI) st1 [dst1]=val1[PIPE_DEPTH-1],1)
	br.ctop.dptk.few 5b
	;;
	mov ar.lc=saved_lc
	mov pr=saved_pr,0xffffffffffff0000
	mov ar.pfs=saved_pfs
	br.ret.sptk.many rp

	//
	// Beginning of long mempcy (i.e. > 16 bytes)
	//
.long_copy_user:
	tbit.nz p6,p7=src1,0	// odd alignment
	and tmp=7,tmp
	;;
	cmp.eq p10,p8=r0,tmp
	mov len1=len		// copy because of rotation
(p8)	br.cond.dpnt .diff_align_copy_user
	;;
	// At this point we know we have more than 16 bytes to copy
	// and also that both src and dest have the same alignment
	// which may not be the one we want. So for now we must move
	// forward slowly until we reach 16byte alignment: no need to
	// worry about reaching the end of buffer.
	//
	EX(.failure_in1,(p6) ld1 val1[0]=[src1],1)	// 1-byte aligned
(p6)	adds len1=-1,len1;;
	tbit.nz p7,p0=src1,1
	;;
	EX(.failure_in1,(p7) ld2 val1[1]=[src1],2)	// 2-byte aligned
(p7)	adds len1=-2,len1;;
	tbit.nz p8,p0=src1,2
	;;
	//
	// Stop bit not required after ld4 because if we fail on ld4
	// we have never executed the ld1, therefore st1 is not executed.
	//
	EX(.failure_in1,(p8) ld4 val2[0]=[src1],4)	// 4-byte aligned
	;;
	EX(.failure_out,(p6) st1 [dst1]=val1[0],1)
	tbit.nz p9,p0=src1,3
	;;
	//
	// Stop bit not required after ld8 because if we fail on ld8
	// we have never executed the ld2, therefore st2 is not executed.
	//
	EX(.failure_in1,(p9) ld8 val2[1]=[src1],8)	// 8-byte aligned
	EX(.failure_out,(p7) st2 [dst1]=val1[1],2)
(p8)	adds len1=-4,len1
	;;
	EX(.failure_out, (p8) st4 [dst1]=val2[0],4)
(p9)	adds len1=-8,len1;;
	shr.u cnt=len1,4		// number of 128-bit (2x64bit) words
	;;
	EX(.failure_out, (p9) st8 [dst1]=val2[1],8)
	tbit.nz p6,p0=len1,3
	cmp.eq p7,p0=r0,cnt
	adds tmp=-1,cnt			// br.ctop is repeat/until
(p7)	br.cond.dpnt .dotail		// we have less than 16 bytes left
	;;
	adds src2=8,src1
	adds dst2=8,dst1
	mov ar.lc=tmp
	;;
	//
	// 16bytes/iteration
	//
2:
	EX(.failure_in3,(p16) ld8 val1[0]=[src1],16)
(p16)	ld8 val2[0]=[src2],16

	EX(.failure_out, (EPI)	st8 [dst1]=val1[PIPE_DEPTH-1],16)
(EPI)	st8 [dst2]=val2[PIPE_DEPTH-1],16
	br.ctop.dptk 2b
	;;			// RAW on src1 when fall through from loop
	//
	// Tail correction based on len only
	//
	// No matter where we come from (loop or test) the src1 pointer
	// is 16 byte aligned AND we have less than 16 bytes to copy.
	//
.dotail:
	EX(.failure_in1,(p6) ld8 val1[0]=[src1],8)	// at least 8 bytes
	tbit.nz p7,p0=len1,2
	;;
	EX(.failure_in1,(p7) ld4 val1[1]=[src1],4)	// at least 4 bytes
	tbit.nz p8,p0=len1,1
	;;
	EX(.failure_in1,(p8) ld2 val2[0]=[src1],2)	// at least 2 bytes
	tbit.nz p9,p0=len1,0
	;;
	EX(.failure_out, (p6) st8 [dst1]=val1[0],8)
	;;
	EX(.failure_in1,(p9) ld1 val2[1]=[src1])	// only 1 byte left
	mov ar.lc=saved_lc
	;;
	EX(.failure_out,(p7) st4 [dst1]=val1[1],4)
	mov pr=saved_pr,0xffffffffffff0000
	;;
	EX(.failure_out, (p8)	st2 [dst1]=val2[0],2)
	mov ar.pfs=saved_pfs
	;;
	EX(.failure_out, (p9)	st1 [dst1]=val2[1])
	br.ret.sptk.many rp


	//
	// Here we handle the case where the byte by byte copy fails
	// on the load.
	// Several factors make the zeroing of the rest of the buffer kind of
	// tricky:
	//	- the pipeline: loads/stores are not in sync (pipeline)
	//
	//	  In the same loop iteration, the dst1 pointer does not directly
	//	  reflect where the faulty load was.
	//
	//	- pipeline effect
	//	  When you get a fault on load, you may have valid data from
	//	  previous loads not yet store in transit. Such data must be
	//	  store normally before moving onto zeroing the rest.
	//
	//	- single/multi dispersal independence.
	//
	// solution:
	//	- we don't disrupt the pipeline, i.e. data in transit in
	//	  the software pipeline will be eventually move to memory.
	//	  We simply replace the load with a simple mov and keep the
	//	  pipeline going. We can't really do this inline because
	//	  p16 is always reset to 1 when lc > 0.
	//
.failure_in_pipe1:
	sub ret0=endsrc,src1	// number of bytes to zero, i.e. not copied
1:
(p16)	mov val1[0]=r0
(EPI)	st1 [dst1]=val1[PIPE_DEPTH-1],1
	br.ctop.dptk 1b
	;;
	mov pr=saved_pr,0xffffffffffff0000
	mov ar.lc=saved_lc
	mov ar.pfs=saved_pfs
	br.ret.sptk.many rp

	//
	// This is the case where the byte by byte copy fails on the load
	// when we copy the head. We need to finish the pipeline and copy
	// zeros for the rest of the destination. Since this happens
	// at the top we still need to fill the body and tail.
.failure_in_pipe2:
	sub ret0=endsrc,src1	// number of bytes to zero, i.e. not copied
2:
(p16)	mov val1[0]=r0
(EPI)	st1 [dst1]=val1[PIPE_DEPTH-1],1
	br.ctop.dptk 2b
	;;
	sub len=enddst,dst1,1		// precompute len
	br.cond.dptk.many .failure_in1bis
	;;

	//
	// Here we handle the head & tail part when we check for alignment.
	// The following code handles only the load failures. The
	// main diffculty comes from the fact that loads/stores are
	// scheduled. So when you fail on a load, the stores corresponding
	// to previous successful loads must be executed.
	//
	// However some simplifications are possible given the way
	// things work.
	//
	// 1) HEAD
	// Theory of operation:
	//
	//  Page A   | Page B
	//  ---------|-----
	//          1|8 x
	//	  1 2|8 x
	//	    4|8 x
	//	  1 4|8 x
	//        2 4|8 x
	//      1 2 4|8 x
	//	     |1
	//	     |2 x
	//	     |4 x
	//
	// page_size >= 4k (2^12).  (x means 4, 2, 1)
	// Here we suppose Page A exists and Page B does not.
	//
	// As we move towards eight byte alignment we may encounter faults.
	// The numbers on each page show the size of the load (current alignment).
	//
	// Key point:
	//	- if you fail on 1, 2, 4 then you have never executed any smaller
	//	  size loads, e.g. failing ld4 means no ld1 nor ld2 executed
	//	  before.
	//
	// This allows us to simplify the cleanup code, because basically you
	// only have to worry about "pending" stores in the case of a failing
	// ld8(). Given the way the code is written today, this means only
	// worry about st2, st4. There we can use the information encapsulated
	// into the predicates.
	//
	// Other key point:
	//	- if you fail on the ld8 in the head, it means you went straight
	//	  to it, i.e. 8byte alignment within an unexisting page.
	// Again this comes from the fact that if you crossed just for the ld8 then
	// you are 8byte aligned but also 16byte align, therefore you would
	// either go for the 16byte copy loop OR the ld8 in the tail part.
	// The combination ld1, ld2, ld4, ld8 where you fail on ld8 is impossible
	// because it would mean you had 15bytes to copy in which case you
	// would have defaulted to the byte by byte copy.
	//
	//
	// 2) TAIL
	// Here we now we have less than 16 bytes AND we are either 8 or 16 byte
	// aligned.
	//
	// Key point:
	// This means that we either:
	//		- are right on a page boundary
	//	OR
	//		- are at more than 16 bytes from a page boundary with
	//		  at most 15 bytes to copy: no chance of crossing.
	//
	// This allows us to assume that if we fail on a load we haven't possibly
	// executed any of the previous (tail) ones, so we don't need to do
	// any stores. For instance, if we fail on ld2, this means we had
	// 2 or 3 bytes left to copy and we did not execute the ld8 nor ld4.
	//
	// This means that we are in a situation similar the a fault in the
	// head part. That's nice!
	//
.failure_in1:
	sub ret0=endsrc,src1	// number of bytes to zero, i.e. not copied
	sub len=endsrc,src1,1
	//
	// we know that ret0 can never be zero at this point
	// because we failed why trying to do a load, i.e. there is still
	// some work to do.
	// The failure_in1bis and length problem is taken care of at the
	// calling side.
	//
	;;
.failure_in1bis:		// from (.failure_in3)
	mov ar.lc=len		// Continue with a stupid byte store.
	;;
5:
	st1 [dst1]=r0,1
	br.cloop.dptk 5b
	;;
	mov pr=saved_pr,0xffffffffffff0000
	mov ar.lc=saved_lc
	mov ar.pfs=saved_pfs
	br.ret.sptk.many rp

	//
	// Here we simply restart the loop but instead
	// of doing loads we fill the pipeline with zeroes
	// We can't simply store r0 because we may have valid
	// data in transit in the pipeline.
	// ar.lc and ar.ec are setup correctly at this point
	//
	// we MUST use src1/endsrc here and not dst1/enddst because
	// of the pipeline effect.
	//
.failure_in3:
	sub ret0=endsrc,src1	// number of bytes to zero, i.e. not copied
	;;
2:
(p16)	mov val1[0]=r0
(p16)	mov val2[0]=r0
(EPI)	st8 [dst1]=val1[PIPE_DEPTH-1],16
(EPI)	st8 [dst2]=val2[PIPE_DEPTH-1],16
	br.ctop.dptk 2b
	;;
	cmp.ne p6,p0=dst1,enddst	// Do we need to finish the tail ?
	sub len=enddst,dst1,1		// precompute len
(p6)	br.cond.dptk .failure_in1bis
	;;
	mov pr=saved_pr,0xffffffffffff0000
	mov ar.lc=saved_lc
	mov ar.pfs=saved_pfs
	br.ret.sptk.many rp

.failure_in2:
	sub ret0=endsrc,src1
	cmp.ne p6,p0=dst1,enddst	// Do we need to finish the tail ?
	sub len=enddst,dst1,1		// precompute len
(p6)	br.cond.dptk .failure_in1bis
	;;
	mov pr=saved_pr,0xffffffffffff0000
	mov ar.lc=saved_lc
	mov ar.pfs=saved_pfs
	br.ret.sptk.many rp

	//
	// handling of failures on stores: that's the easy part
	//
.failure_out:
	sub ret0=enddst,dst1
	mov pr=saved_pr,0xffffffffffff0000
	mov ar.lc=saved_lc

	mov ar.pfs=saved_pfs
	br.ret.sptk.many rp
END(__copy_user)
EXPORT_SYMBOL(__copy_user)
