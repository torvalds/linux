/*
 * This routine clears to zero a linear memory buffer in user space.
 *
 * Inputs:
 *	in0:	address of buffer
 *	in1:	length of buffer in bytes
 * Outputs:
 *	r8:	number of bytes that didn't get cleared due to a fault
 *
 * Copyright (C) 1998, 1999, 2001 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 */

#include <asm/asmmacro.h>

//
// arguments
//
#define buf		r32
#define len		r33

//
// local registers
//
#define cnt		r16
#define buf2		r17
#define saved_lc	r18
#define saved_pfs	r19
#define tmp		r20
#define len2		r21
#define len3		r22

//
// Theory of operations:
//	- we check whether or not the buffer is small, i.e., less than 17
//	  in which case we do the byte by byte loop.
//
//	- Otherwise we go progressively from 1 byte store to 8byte store in
//	  the head part, the body is a 16byte store loop and we finish we the
//	  tail for the last 15 bytes.
//	  The good point about this breakdown is that the long buffer handling
//	  contains only 2 branches.
//
//	The reason for not using shifting & masking for both the head and the
//	tail is to stay semantically correct. This routine is not supposed
//	to write bytes outside of the buffer. While most of the time this would
//	be ok, we can't tolerate a mistake. A classical example is the case
//	of multithreaded code were to the extra bytes touched is actually owned
//	by another thread which runs concurrently to ours. Another, less likely,
//	example is with device drivers where reading an I/O mapped location may
//	have side effects (same thing for writing).
//

GLOBAL_ENTRY(__do_clear_user)
	.prologue
	.save ar.pfs, saved_pfs
	alloc	saved_pfs=ar.pfs,2,0,0,0
	cmp.eq p6,p0=r0,len		// check for zero length
	.save ar.lc, saved_lc
	mov saved_lc=ar.lc		// preserve ar.lc (slow)
	.body
	;;				// avoid WAW on CFM
	adds tmp=-1,len			// br.ctop is repeat/until
	mov ret0=len			// return value is length at this point
(p6)	br.ret.spnt.many rp
	;;
	cmp.lt p6,p0=16,len		// if len > 16 then long memset
	mov ar.lc=tmp			// initialize lc for small count
(p6)	br.cond.dptk .long_do_clear
	;;				// WAR on ar.lc
	//
	// worst case 16 iterations, avg 8 iterations
	//
	// We could have played with the predicates to use the extra
	// M slot for 2 stores/iteration but the cost the initialization
	// the various counters compared to how long the loop is supposed
	// to last on average does not make this solution viable.
	//
1:
	EX( .Lexit1, st1 [buf]=r0,1 )
	adds len=-1,len			// countdown length using len
	br.cloop.dptk 1b
	;;				// avoid RAW on ar.lc
	//
	// .Lexit4: comes from byte by byte loop
	//	    len contains bytes left
.Lexit1:
	mov ret0=len			// faster than using ar.lc
	mov ar.lc=saved_lc
	br.ret.sptk.many rp		// end of short clear_user


	//
	// At this point we know we have more than 16 bytes to copy
	// so we focus on alignment (no branches required)
	//
	// The use of len/len2 for countdown of the number of bytes left
	// instead of ret0 is due to the fact that the exception code
	// changes the values of r8.
	//
.long_do_clear:
	tbit.nz p6,p0=buf,0		// odd alignment (for long_do_clear)
	;;
	EX( .Lexit3, (p6) st1 [buf]=r0,1 )	// 1-byte aligned
(p6)	adds len=-1,len;;		// sync because buf is modified
	tbit.nz p6,p0=buf,1
	;;
	EX( .Lexit3, (p6) st2 [buf]=r0,2 )	// 2-byte aligned
(p6)	adds len=-2,len;;
	tbit.nz p6,p0=buf,2
	;;
	EX( .Lexit3, (p6) st4 [buf]=r0,4 )	// 4-byte aligned
(p6)	adds len=-4,len;;
	tbit.nz p6,p0=buf,3
	;;
	EX( .Lexit3, (p6) st8 [buf]=r0,8 )	// 8-byte aligned
(p6)	adds len=-8,len;;
	shr.u cnt=len,4		// number of 128-bit (2x64bit) words
	;;
	cmp.eq p6,p0=r0,cnt
	adds tmp=-1,cnt
(p6)	br.cond.dpnt .dotail		// we have less than 16 bytes left
	;;
	adds buf2=8,buf			// setup second base pointer
	mov ar.lc=tmp
	;;

	//
	// 16bytes/iteration core loop
	//
	// The second store can never generate a fault because
	// we come into the loop only when we are 16-byte aligned.
	// This means that if we cross a page then it will always be
	// in the first store and never in the second.
	//
	//
	// We need to keep track of the remaining length. A possible (optimistic)
	// way would be to use ar.lc and derive how many byte were left by
	// doing : left= 16*ar.lc + 16.  this would avoid the addition at
	// every iteration.
	// However we need to keep the synchronization point. A template
	// M;;MB does not exist and thus we can keep the addition at no
	// extra cycle cost (use a nop slot anyway). It also simplifies the
	// (unlikely)  error recovery code
	//

2:	EX(.Lexit3, st8 [buf]=r0,16 )
	;;				// needed to get len correct when error
	st8 [buf2]=r0,16
	adds len=-16,len
	br.cloop.dptk 2b
	;;
	mov ar.lc=saved_lc
	//
	// tail correction based on len only
	//
	// We alternate the use of len3,len2 to allow parallelism and correct
	// error handling. We also reuse p6/p7 to return correct value.
	// The addition of len2/len3 does not cost anything more compared to
	// the regular memset as we had empty slots.
	//
.dotail:
	mov len2=len			// for parallelization of error handling
	mov len3=len
	tbit.nz p6,p0=len,3
	;;
	EX( .Lexit2, (p6) st8 [buf]=r0,8 )	// at least 8 bytes
(p6)	adds len3=-8,len2
	tbit.nz p7,p6=len,2
	;;
	EX( .Lexit2, (p7) st4 [buf]=r0,4 )	// at least 4 bytes
(p7)	adds len2=-4,len3
	tbit.nz p6,p7=len,1
	;;
	EX( .Lexit2, (p6) st2 [buf]=r0,2 )	// at least 2 bytes
(p6)	adds len3=-2,len2
	tbit.nz p7,p6=len,0
	;;
	EX( .Lexit2, (p7) st1 [buf]=r0 )	// only 1 byte left
	mov ret0=r0				// success
	br.ret.sptk.many rp			// end of most likely path

	//
	// Outlined error handling code
	//

	//
	// .Lexit3: comes from core loop, need restore pr/lc
	//	    len contains bytes left
	//
	//
	// .Lexit2:
	//	if p6 -> coming from st8 or st2 : len2 contains what's left
	//	if p7 -> coming from st4 or st1 : len3 contains what's left
	// We must restore lc/pr even though might not have been used.
.Lexit2:
	.pred.rel "mutex", p6, p7
(p6)	mov len=len2
(p7)	mov len=len3
	;;
	//
	// .Lexit4: comes from head, need not restore pr/lc
	//	    len contains bytes left
	//
.Lexit3:
	mov ret0=len
	mov ar.lc=saved_lc
	br.ret.sptk.many rp
END(__do_clear_user)
