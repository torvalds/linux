/*
 * Cache flushing routines.
 *
 * Copyright (C) 1999-2001, 2005 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 05/28/05 Zoltan Menyhart	Dynamic stride size
 */

#include <asm/asmmacro.h>
#include <asm/export.h>


	/*
	 * flush_icache_range(start,end)
	 *
	 *	Make i-cache(s) coherent with d-caches.
	 *
	 *	Must deal with range from start to end-1 but nothing else (need to
	 *	be careful not to touch addresses that may be unmapped).
	 *
	 *	Note: "in0" and "in1" are preserved for debugging purposes.
	 */
	.section .kprobes.text,"ax"
GLOBAL_ENTRY(flush_icache_range)

	.prologue
	alloc	r2=ar.pfs,2,0,0,0
	movl	r3=ia64_i_cache_stride_shift
 	mov	r21=1
	;;
	ld8	r20=[r3]		// r20: stride shift
	sub	r22=in1,r0,1		// last byte address
	;;
	shr.u	r23=in0,r20		// start / (stride size)
	shr.u	r22=r22,r20		// (last byte address) / (stride size)
	shl	r21=r21,r20		// r21: stride size of the i-cache(s)
	;;
	sub	r8=r22,r23		// number of strides - 1
	shl	r24=r23,r20		// r24: addresses for "fc.i" =
					//	"start" rounded down to stride boundary
	.save	ar.lc,r3
	mov	r3=ar.lc		// save ar.lc
	;;

	.body
	mov	ar.lc=r8
	;;
	/*
	 * 32 byte aligned loop, even number of (actually 2) bundles
	 */
.Loop:	fc.i	r24			// issuable on M0 only
	add	r24=r21,r24		// we flush "stride size" bytes per iteration
	nop.i	0
	br.cloop.sptk.few .Loop
	;;
	sync.i
	;;
	srlz.i
	;;
	mov	ar.lc=r3		// restore ar.lc
	br.ret.sptk.many rp
END(flush_icache_range)
EXPORT_SYMBOL_GPL(flush_icache_range)

	/*
	 * clflush_cache_range(start,size)
	 *
	 *	Flush cache lines from start to start+size-1.
	 *
	 *	Must deal with range from start to start+size-1 but nothing else
	 *	(need to be careful not to touch addresses that may be
	 *	unmapped).
	 *
	 *	Note: "in0" and "in1" are preserved for debugging purposes.
	 */
	.section .kprobes.text,"ax"
GLOBAL_ENTRY(clflush_cache_range)

	.prologue
	alloc	r2=ar.pfs,2,0,0,0
	movl	r3=ia64_cache_stride_shift
	mov	r21=1
	add     r22=in1,in0
	;;
	ld8	r20=[r3]		// r20: stride shift
	sub	r22=r22,r0,1		// last byte address
	;;
	shr.u	r23=in0,r20		// start / (stride size)
	shr.u	r22=r22,r20		// (last byte address) / (stride size)
	shl	r21=r21,r20		// r21: stride size of the i-cache(s)
	;;
	sub	r8=r22,r23		// number of strides - 1
	shl	r24=r23,r20		// r24: addresses for "fc" =
					//	"start" rounded down to stride
					//	boundary
	.save	ar.lc,r3
	mov	r3=ar.lc		// save ar.lc
	;;

	.body
	mov	ar.lc=r8
	;;
	/*
	 * 32 byte aligned loop, even number of (actually 2) bundles
	 */
.Loop_fc:
	fc	r24		// issuable on M0 only
	add	r24=r21,r24	// we flush "stride size" bytes per iteration
	nop.i	0
	br.cloop.sptk.few .Loop_fc
	;;
	sync.i
	;;
	srlz.i
	;;
	mov	ar.lc=r3		// restore ar.lc
	br.ret.sptk.many rp
END(clflush_cache_range)
