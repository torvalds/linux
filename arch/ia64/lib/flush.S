/*
 * Cache flushing routines.
 *
 * Copyright (C) 1999-2001 Hewlett-Packard Co
 * Copyright (C) 1999-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 */
#include <asm/asmmacro.h>
#include <asm/page.h>

	/*
	 * flush_icache_range(start,end)
	 *	Must flush range from start to end-1 but nothing else (need to
	 *	be careful not to touch addresses that may be unmapped).
	 */
GLOBAL_ENTRY(flush_icache_range)
	.prologue
	alloc r2=ar.pfs,2,0,0,0
	sub r8=in1,in0,1
	;;
	shr.u r8=r8,5			// we flush 32 bytes per iteration
	.save ar.lc, r3
	mov r3=ar.lc			// save ar.lc
	;;

	.body

	mov ar.lc=r8
	;;
.Loop:	fc in0				// issuable on M0 only
	add in0=32,in0
	br.cloop.sptk.few .Loop
	;;
	sync.i
	;;
	srlz.i
	;;
	mov ar.lc=r3			// restore ar.lc
	br.ret.sptk.many rp
END(flush_icache_range)
