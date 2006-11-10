/*
 * Optmized version of the ip_fast_csum() function
 * Used for calculating IP header checksum
 *
 * Return: 16bit checksum, complemented
 *
 * Inputs:
 *      in0: address of buffer to checksum (char *)
 *      in1: length of the buffer (int)
 *
 * Copyright (C) 2002, 2006 Intel Corp.
 * Copyright (C) 2002, 2006 Ken Chen <kenneth.w.chen@intel.com>
 */

#include <asm/asmmacro.h>

/*
 * Since we know that most likely this function is called with buf aligned
 * on 4-byte boundary and 20 bytes in length, we can execution rather quickly
 * versus calling generic version of do_csum, which has lots of overhead in
 * handling various alignments and sizes.  However, due to lack of constrains
 * put on the function input argument, cases with alignment not on 4-byte or
 * size not equal to 20 bytes will be handled by the generic do_csum function.
 */

#define in0	r32
#define in1	r33
#define in2	r34
#define in3	r35
#define in4	r36
#define ret0	r8

GLOBAL_ENTRY(ip_fast_csum)
	.prologue
	.body
	cmp.ne	p6,p7=5,in1	// size other than 20 byte?
	and	r14=3,in0	// is it aligned on 4-byte?
	add	r15=4,in0	// second source pointer
	;;
	cmp.ne.or.andcm p6,p7=r14,r0
	;;
(p7)	ld4	r20=[in0],8
(p7)	ld4	r21=[r15],8
(p6)	br.spnt	.generic
	;;
	ld4	r22=[in0],8
	ld4	r23=[r15],8
	;;
	ld4	r24=[in0]
	add	r20=r20,r21
	add	r22=r22,r23
	;;
	add	r20=r20,r22
	;;
	add	r20=r20,r24
	;;
	shr.u	ret0=r20,16	// now need to add the carry
	zxt2	r20=r20
	;;
	add	r20=ret0,r20
	;;
	shr.u	ret0=r20,16	// add carry again
	zxt2	r20=r20
	;;
	add	r20=ret0,r20
	;;
	shr.u	ret0=r20,16
	zxt2	r20=r20
	;;
	add	r20=ret0,r20
	;;
	andcm	ret0=-1,r20
	.restore sp		// reset frame state
	br.ret.sptk.many b0
	;;

.generic:
	.prologue
	.save ar.pfs, r35
	alloc	r35=ar.pfs,2,2,2,0
	.save rp, r34
	mov	r34=b0
	.body
	dep.z	out1=in1,2,30
	mov	out0=in0
	;;
	br.call.sptk.many b0=do_csum
	;;
	andcm	ret0=-1,ret0
	mov	ar.pfs=r35
	mov	b0=r34
	br.ret.sptk.many b0
END(ip_fast_csum)

GLOBAL_ENTRY(csum_ipv6_magic)
	ld4	r20=[in0],4
	ld4	r21=[in1],4
	dep	r15=in3,in2,32,16
	;;
	ld4	r22=[in0],4
	ld4	r23=[in1],4
	mux1	r15=r15,@rev
	;;
	ld4	r24=[in0],4
	ld4	r25=[in1],4
	shr.u	r15=r15,16
	add	r16=r20,r21
	add	r17=r22,r23
	;;
	ld4	r26=[in0],4
	ld4	r27=[in1],4
	add	r18=r24,r25
	add	r8=r16,r17
	;;
	add	r19=r26,r27
	add	r8=r8,r18
	;;
	add	r8=r8,r19
	add	r15=r15,in4
	;;
	add	r8=r8,r15
	;;
	shr.u	r10=r8,32	// now fold sum into short
	zxt4	r11=r8
	;;
	add	r8=r10,r11
	;;
	shr.u	r10=r8,16	// yeah, keep it rolling
	zxt2	r11=r8
	;;
	add	r8=r10,r11
	;;
	shr.u	r10=r8,16	// three times lucky
	zxt2	r11=r8
	;;
	add	r8=r10,r11
	mov	r9=0xffff
	;;
	andcm	r8=r9,r8
	br.ret.sptk.many b0
END(csum_ipv6_magic)
