/*
 *
 * Optmized version of the standard do_csum() function
 *
 * Return: a 64bit quantity containing the 16bit Internet checksum
 *
 * Inputs:
 *	in0: address of buffer to checksum (char *)
 *	in1: length of the buffer (int)
 *
 * Copyright (C) 1999, 2001-2002 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * 02/04/22	Ken Chen <kenneth.w.chen@intel.com>
 *		Data locality study on the checksum buffer.
 *		More optimization cleanup - remove excessive stop bits.
 * 02/04/08	David Mosberger <davidm@hpl.hp.com>
 *		More cleanup and tuning.
 * 01/04/18	Jun Nakajima <jun.nakajima@intel.com>
 *		Clean up and optimize and the software pipeline, loading two
 *		back-to-back 8-byte words per loop. Clean up the initialization
 *		for the loop. Support the cases where load latency = 1 or 2.
 *		Set CONFIG_IA64_LOAD_LATENCY to 1 or 2 (default).
 */

#include <asm/asmmacro.h>

//
// Theory of operations:
//	The goal is to go as quickly as possible to the point where
//	we can checksum 16 bytes/loop. Before reaching that point we must
//	take care of incorrect alignment of first byte.
//
//	The code hereafter also takes care of the "tail" part of the buffer
//	before entering the core loop, if any. The checksum is a sum so it
//	allows us to commute operations. So we do the "head" and "tail"
//	first to finish at full speed in the body. Once we get the head and
//	tail values, we feed them into the pipeline, very handy initialization.
//
//	Of course we deal with the special case where the whole buffer fits
//	into one 8 byte word. In this case we have only one entry in the pipeline.
//
//	We use a (LOAD_LATENCY+2)-stage pipeline in the loop to account for
//	possible load latency and also to accommodate for head and tail.
//
//	The end of the function deals with folding the checksum from 64bits
//	down to 16bits taking care of the carry.
//
//	This version avoids synchronization in the core loop by also using a
//	pipeline for the accumulation of the checksum in resultx[] (x=1,2).
//
//	 wordx[] (x=1,2)
//	|---|
//      |   | 0			: new value loaded in pipeline
//	|---|
//      |   | -			: in transit data
//	|---|
//      |   | LOAD_LATENCY	: current value to add to checksum
//	|---|
//      |   | LOAD_LATENCY+1	: previous value added to checksum
//      |---|			(previous iteration)
//
//	resultx[] (x=1,2)
//	|---|
//      |   | 0			: initial value
//	|---|
//      |   | LOAD_LATENCY-1	: new checksum
//	|---|
//      |   | LOAD_LATENCY	: previous value of checksum
//	|---|
//      |   | LOAD_LATENCY+1	: final checksum when out of the loop
//      |---|
//
//
//	See RFC1071 "Computing the Internet Checksum" for various techniques for
//	calculating the Internet checksum.
//
// NOT YET DONE:
//	- Maybe another algorithm which would take care of the folding at the
//	  end in a different manner
//	- Work with people more knowledgeable than me on the network stack
//	  to figure out if we could not split the function depending on the
//	  type of packet or alignment we get. Like the ip_fast_csum() routine
//	  where we know we have at least 20bytes worth of data to checksum.
//	- Do a better job of handling small packets.
//	- Note on prefetching: it was found that under various load, i.e. ftp read/write,
//	  nfs read/write, the L1 cache hit rate is at 60% and L2 cache hit rate is at 99.8%
//	  on the data that buffer points to (partly because the checksum is often preceded by
//	  a copy_from_user()).  This finding indiate that lfetch will not be beneficial since
//	  the data is already in the cache.
//

#define saved_pfs	r11
#define hmask		r16
#define tmask		r17
#define first1		r18
#define firstval	r19
#define firstoff	r20
#define last		r21
#define lastval		r22
#define lastoff		r23
#define saved_lc	r24
#define saved_pr	r25
#define tmp1		r26
#define tmp2		r27
#define tmp3		r28
#define carry1		r29
#define carry2		r30
#define first2		r31

#define buf		in0
#define len		in1

#define LOAD_LATENCY	2	// XXX fix me

#if (LOAD_LATENCY != 1) && (LOAD_LATENCY != 2)
# error "Only 1 or 2 is supported/tested for LOAD_LATENCY."
#endif

#define PIPE_DEPTH			(LOAD_LATENCY+2)
#define ELD	p[LOAD_LATENCY]		// end of load
#define ELD_1	p[LOAD_LATENCY+1]	// and next stage

// unsigned long do_csum(unsigned char *buf,long len)

GLOBAL_ENTRY(do_csum)
	.prologue
	.save ar.pfs, saved_pfs
	alloc saved_pfs=ar.pfs,2,16,0,16
	.rotr word1[4], word2[4],result1[LOAD_LATENCY+2],result2[LOAD_LATENCY+2]
	.rotp p[PIPE_DEPTH], pC1[2], pC2[2]
	mov ret0=r0		// in case we have zero length
	cmp.lt p0,p6=r0,len	// check for zero length or negative (32bit len)
	;;
	add tmp1=buf,len	// last byte's address
	.save pr, saved_pr
	mov saved_pr=pr		// preserve predicates (rotation)
(p6)	br.ret.spnt.many rp	// return if zero or negative length

	mov hmask=-1		// initialize head mask
	tbit.nz p15,p0=buf,0	// is buf an odd address?
	and first1=-8,buf	// 8-byte align down address of first1 element

	and firstoff=7,buf	// how many bytes off for first1 element
	mov tmask=-1		// initialize tail mask

	;;
	adds tmp2=-1,tmp1	// last-1
	and lastoff=7,tmp1	// how many bytes off for last element
	;;
	sub tmp1=8,lastoff	// complement to lastoff
	and last=-8,tmp2	// address of word containing last byte
	;;
	sub tmp3=last,first1	// tmp3=distance from first1 to last
	.save ar.lc, saved_lc
	mov saved_lc=ar.lc	// save lc
	cmp.eq p8,p9=last,first1	// everything fits in one word ?

	ld8 firstval=[first1],8	// load, ahead of time, "first1" word
	and tmp1=7, tmp1	// make sure that if tmp1==8 -> tmp1=0
	shl tmp2=firstoff,3	// number of bits
	;;
(p9)	ld8 lastval=[last]	// load, ahead of time, "last" word, if needed
	shl tmp1=tmp1,3		// number of bits
(p9)	adds tmp3=-8,tmp3	// effectively loaded
	;;
(p8)	mov lastval=r0		// we don't need lastval if first1==last
	shl hmask=hmask,tmp2	// build head mask, mask off [0,first1off[
	shr.u tmask=tmask,tmp1	// build tail mask, mask off ]8,lastoff]
	;;
	.body
#define count tmp3

(p8)	and hmask=hmask,tmask	// apply tail mask to head mask if 1 word only
(p9)	and word2[0]=lastval,tmask	// mask last it as appropriate
	shr.u count=count,3	// how many 8-byte?
	;;
	// If count is odd, finish this 8-byte word so that we can
	// load two back-to-back 8-byte words per loop thereafter.
	and word1[0]=firstval,hmask	// and mask it as appropriate
	tbit.nz p10,p11=count,0		// if (count is odd)
	;;
(p8)	mov result1[0]=word1[0]
(p9)	add result1[0]=word1[0],word2[0]
	;;
	cmp.ltu p6,p0=result1[0],word1[0]	// check the carry
	cmp.eq.or.andcm p8,p0=0,count		// exit if zero 8-byte
	;;
(p6)	adds result1[0]=1,result1[0]
(p8)	br.cond.dptk .do_csum_exit	// if (within an 8-byte word)
(p11)	br.cond.dptk .do_csum16		// if (count is even)

	// Here count is odd.
	ld8 word1[1]=[first1],8		// load an 8-byte word
	cmp.eq p9,p10=1,count		// if (count == 1)
	adds count=-1,count		// loaded an 8-byte word
	;;
	add result1[0]=result1[0],word1[1]
	;;
	cmp.ltu p6,p0=result1[0],word1[1]
	;;
(p6)	adds result1[0]=1,result1[0]
(p9)	br.cond.sptk .do_csum_exit	// if (count == 1) exit
	// Fall through to calculate the checksum, feeding result1[0] as
	// the initial value in result1[0].
	//
	// Calculate the checksum loading two 8-byte words per loop.
	//
.do_csum16:
	add first2=8,first1
	shr.u count=count,1	// we do 16 bytes per loop
	;;
	adds count=-1,count
	mov carry1=r0
	mov carry2=r0
	brp.loop.imp 1f,2f
	;;
	mov ar.ec=PIPE_DEPTH
	mov ar.lc=count	// set lc
	mov pr.rot=1<<16
	// result1[0] must be initialized in advance.
	mov result2[0]=r0
	;;
	.align 32
1:
(ELD_1)	cmp.ltu pC1[0],p0=result1[LOAD_LATENCY],word1[LOAD_LATENCY+1]
(pC1[1])adds carry1=1,carry1
(ELD_1)	cmp.ltu pC2[0],p0=result2[LOAD_LATENCY],word2[LOAD_LATENCY+1]
(pC2[1])adds carry2=1,carry2
(ELD)	add result1[LOAD_LATENCY-1]=result1[LOAD_LATENCY],word1[LOAD_LATENCY]
(ELD)	add result2[LOAD_LATENCY-1]=result2[LOAD_LATENCY],word2[LOAD_LATENCY]
2:
(p[0])	ld8 word1[0]=[first1],16
(p[0])	ld8 word2[0]=[first2],16
	br.ctop.sptk 1b
	;;
	// Since len is a 32-bit value, carry cannot be larger than a 64-bit value.
(pC1[1])adds carry1=1,carry1	// since we miss the last one
(pC2[1])adds carry2=1,carry2
	;;
	add result1[LOAD_LATENCY+1]=result1[LOAD_LATENCY+1],carry1
	add result2[LOAD_LATENCY+1]=result2[LOAD_LATENCY+1],carry2
	;;
	cmp.ltu p6,p0=result1[LOAD_LATENCY+1],carry1
	cmp.ltu p7,p0=result2[LOAD_LATENCY+1],carry2
	;;
(p6)	adds result1[LOAD_LATENCY+1]=1,result1[LOAD_LATENCY+1]
(p7)	adds result2[LOAD_LATENCY+1]=1,result2[LOAD_LATENCY+1]
	;;
	add result1[0]=result1[LOAD_LATENCY+1],result2[LOAD_LATENCY+1]
	;;
	cmp.ltu p6,p0=result1[0],result2[LOAD_LATENCY+1]
	;;
(p6)	adds result1[0]=1,result1[0]
	;;
.do_csum_exit:
	//
	// now fold 64 into 16 bits taking care of carry
	// that's not very good because it has lots of sequentiality
	//
	mov tmp3=0xffff
	zxt4 tmp1=result1[0]
	shr.u tmp2=result1[0],32
	;;
	add result1[0]=tmp1,tmp2
	;;
	and tmp1=result1[0],tmp3
	shr.u tmp2=result1[0],16
	;;
	add result1[0]=tmp1,tmp2
	;;
	and tmp1=result1[0],tmp3
	shr.u tmp2=result1[0],16
	;;
	add result1[0]=tmp1,tmp2
	;;
	and tmp1=result1[0],tmp3
	shr.u tmp2=result1[0],16
	;;
	add ret0=tmp1,tmp2
	mov pr=saved_pr,0xffffffffffff0000
	;;
	// if buf was odd then swap bytes
	mov ar.pfs=saved_pfs		// restore ar.ec
(p15)	mux1 ret0=ret0,@rev		// reverse word
	;;
	mov ar.lc=saved_lc
(p15)	shr.u ret0=ret0,64-16	// + shift back to position = swap bytes
	br.ret.sptk.many rp

//	I (Jun Nakajima) wrote an equivalent code (see below), but it was
//	not much better than the original. So keep the original there so that
//	someone else can challenge.
//
//	shr.u word1[0]=result1[0],32
//	zxt4 result1[0]=result1[0]
//	;;
//	add result1[0]=result1[0],word1[0]
//	;;
//	zxt2 result2[0]=result1[0]
//	extr.u word1[0]=result1[0],16,16
//	shr.u carry1=result1[0],32
//	;;
//	add result2[0]=result2[0],word1[0]
//	;;
//	add result2[0]=result2[0],carry1
//	;;
//	extr.u ret0=result2[0],16,16
//	;;
//	add ret0=ret0,result2[0]
//	;;
//	zxt2 ret0=ret0
//	mov ar.pfs=saved_pfs		 // restore ar.ec
//	mov pr=saved_pr,0xffffffffffff0000
//	;;
//	// if buf was odd then swap bytes
//	mov ar.lc=saved_lc
//(p15)	mux1 ret0=ret0,@rev		// reverse word
//	;;
//(p15)	shr.u ret0=ret0,64-16	// + shift back to position = swap bytes
//	br.ret.sptk.many rp

END(do_csum)
