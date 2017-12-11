/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Optimized version of the standard strlen() function
 *
 *
 * Inputs:
 *	in0	address of string
 *
 * Outputs:
 *	ret0	the number of characters in the string (0 if empty string)
 *	does not count the \0
 *
 * Copyright (C) 1999, 2001 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * 09/24/99 S.Eranian add speculation recovery code
 */

#include <asm/asmmacro.h>
#include <asm/export.h>

//
//
// This is an enhanced version of the basic strlen. it includes a combination
// of compute zero index (czx), parallel comparisons, speculative loads and
// loop unroll using rotating registers.
//
// General Ideas about the algorithm:
//	  The goal is to look at the string in chunks of 8 bytes.
//	  so we need to do a few extra checks at the beginning because the
//	  string may not be 8-byte aligned. In this case we load the 8byte
//	  quantity which includes the start of the string and mask the unused
//	  bytes with 0xff to avoid confusing czx.
//	  We use speculative loads and software pipelining to hide memory
//	  latency and do read ahead safely. This way we defer any exception.
//
//	  Because we don't want the kernel to be relying on particular
//	  settings of the DCR register, we provide recovery code in case
//	  speculation fails. The recovery code is going to "redo" the work using
//	  only normal loads. If we still get a fault then we generate a
//	  kernel panic. Otherwise we return the strlen as usual.
//
//	  The fact that speculation may fail can be caused, for instance, by
//	  the DCR.dm bit being set. In this case TLB misses are deferred, i.e.,
//	  a NaT bit will be set if the translation is not present. The normal
//	  load, on the other hand, will cause the translation to be inserted
//	  if the mapping exists.
//
//	  It should be noted that we execute recovery code only when we need
//	  to use the data that has been speculatively loaded: we don't execute
//	  recovery code on pure read ahead data.
//
// Remarks:
//	- the cmp r0,r0 is used as a fast way to initialize a predicate
//	  register to 1. This is required to make sure that we get the parallel
//	  compare correct.
//
//	- we don't use the epilogue counter to exit the loop but we need to set
//	  it to zero beforehand.
//
//	- after the loop we must test for Nat values because neither the
//	  czx nor cmp instruction raise a NaT consumption fault. We must be
//	  careful not to look too far for a Nat for which we don't care.
//	  For instance we don't need to look at a NaT in val2 if the zero byte
//	  was in val1.
//
//	- Clearly performance tuning is required.
//
//
//
#define saved_pfs	r11
#define	tmp		r10
#define base		r16
#define orig		r17
#define saved_pr	r18
#define src		r19
#define mask		r20
#define val		r21
#define val1		r22
#define val2		r23

GLOBAL_ENTRY(strlen)
	.prologue
	.save ar.pfs, saved_pfs
	alloc saved_pfs=ar.pfs,11,0,0,8 // rotating must be multiple of 8

	.rotr v[2], w[2]	// declares our 4 aliases

	extr.u tmp=in0,0,3	// tmp=least significant 3 bits
	mov orig=in0		// keep trackof initial byte address
	dep src=0,in0,0,3	// src=8byte-aligned in0 address
	.save pr, saved_pr
	mov saved_pr=pr		// preserve predicates (rotation)
	;;

	.body

	ld8 v[1]=[src],8	// must not speculate: can fail here
	shl tmp=tmp,3		// multiply by 8bits/byte
	mov mask=-1		// our mask
	;;
	ld8.s w[1]=[src],8	// speculatively load next
	cmp.eq p6,p0=r0,r0	// sets p6 to true for cmp.and
	sub tmp=64,tmp		// how many bits to shift our mask on the right
	;;
	shr.u	mask=mask,tmp	// zero enough bits to hold v[1] valuable part
	mov ar.ec=r0		// clear epilogue counter (saved in ar.pfs)
	;;
	add base=-16,src	// keep track of aligned base
	or v[1]=v[1],mask	// now we have a safe initial byte pattern
	;;
1:
	ld8.s v[0]=[src],8	// speculatively load next
	czx1.r val1=v[1]	// search 0 byte from right
	czx1.r val2=w[1]	// search 0 byte from right following 8bytes
	;;
	ld8.s w[0]=[src],8	// speculatively load next to next
	cmp.eq.and p6,p0=8,val1	// p6 = p6 and val1==8
	cmp.eq.and p6,p0=8,val2	// p6 = p6 and mask==8
(p6)	br.wtop.dptk 1b		// loop until p6 == 0
	;;
	//
	// We must return try the recovery code iff
	// val1_is_nat || (val1==8 && val2_is_nat)
	//
	// XXX Fixme
	//	- there must be a better way of doing the test
	//
	cmp.eq  p8,p9=8,val1	// p6 = val1 had zero (disambiguate)
	tnat.nz p6,p7=val1	// test NaT on val1
(p6)	br.cond.spnt .recover	// jump to recovery if val1 is NaT
	;;
	//
	// if we come here p7 is true, i.e., initialized for // cmp
	//
	cmp.eq.and  p7,p0=8,val1// val1==8?
	tnat.nz.and p7,p0=val2	// test NaT if val2
(p7)	br.cond.spnt .recover	// jump to recovery if val2 is NaT
	;;
(p8)	mov val1=val2		// the other test got us out of the loop
(p8)	adds src=-16,src	// correct position when 3 ahead
(p9)	adds src=-24,src	// correct position when 4 ahead
	;;
	sub ret0=src,orig	// distance from base
	sub tmp=8,val1		// which byte in word
	mov pr=saved_pr,0xffffffffffff0000
	;;
	sub ret0=ret0,tmp	// adjust
	mov ar.pfs=saved_pfs	// because of ar.ec, restore no matter what
	br.ret.sptk.many rp	// end of normal execution

	//
	// Outlined recovery code when speculation failed
	//
	// This time we don't use speculation and rely on the normal exception
	// mechanism. that's why the loop is not as good as the previous one
	// because read ahead is not possible
	//
	// IMPORTANT:
	// Please note that in the case of strlen() as opposed to strlen_user()
	// we don't use the exception mechanism, as this function is not
	// supposed to fail. If that happens it means we have a bug and the
	// code will cause of kernel fault.
	//
	// XXX Fixme
	//	- today we restart from the beginning of the string instead
	//	  of trying to continue where we left off.
	//
.recover:
	ld8 val=[base],8	// will fail if unrecoverable fault
	;;
	or val=val,mask		// remask first bytes
	cmp.eq p0,p6=r0,r0	// nullify first ld8 in loop
	;;
	//
	// ar.ec is still zero here
	//
2:
(p6)	ld8 val=[base],8	// will fail if unrecoverable fault
	;;
	czx1.r val1=val		// search 0 byte from right
	;;
	cmp.eq p6,p0=8,val1	// val1==8 ?
(p6)	br.wtop.dptk 2b		// loop until p6 == 0
	;;			// (avoid WAW on p63)
	sub ret0=base,orig	// distance from base
	sub tmp=8,val1
	mov pr=saved_pr,0xffffffffffff0000
	;;
	sub ret0=ret0,tmp	// length=now - back -1
	mov ar.pfs=saved_pfs	// because of ar.ec, restore no matter what
	br.ret.sptk.many rp	// end of successful recovery code
END(strlen)
EXPORT_SYMBOL(strlen)
