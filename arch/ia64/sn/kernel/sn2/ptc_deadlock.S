/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2004 Silicon Graphics, Inc. All rights reserved.
 */

#include <asm/sn/shub_mmr.h>

#define DEADLOCKBIT	SH_PIO_WRITE_STATUS_WRITE_DEADLOCK_SHFT
#define WRITECOUNTMASK	SH_PIO_WRITE_STATUS_PENDING_WRITE_COUNT_MASK
#define ALIAS_OFFSET	(SH1_PIO_WRITE_STATUS_0_ALIAS-SH1_PIO_WRITE_STATUS_0)


	.global	sn2_ptc_deadlock_recovery_core
	.proc  	sn2_ptc_deadlock_recovery_core

sn2_ptc_deadlock_recovery_core:
	.regstk 6,0,0,0

	ptc0  	 = in0
	data0 	 = in1
	ptc1  	 = in2
	data1 	 = in3
	piowc 	 = in4
	zeroval  = in5
	piowcphy = r30
	psrsave  = r2
	scr1	 = r16
	scr2	 = r17
	mask	 = r18


	extr.u	piowcphy=piowc,0,61;;	// Convert piowc to uncached physical address
	dep	piowcphy=-1,piowcphy,63,1
	movl	mask=WRITECOUNTMASK

1:
	add	scr2=ALIAS_OFFSET,piowc	// Address of WRITE_STATUS alias register 
	mov	scr1=7;;		// Clear DEADLOCK, WRITE_ERROR, MULTI_WRITE_ERROR
	st8.rel	[scr2]=scr1;;

5:	ld8.acq	scr1=[piowc];;		// Wait for PIOs to complete.
	and	scr2=scr1,mask;;	// mask of writecount bits
	cmp.ne	p6,p0=zeroval,scr2
(p6)	br.cond.sptk 5b
	


	////////////// BEGIN PHYSICAL MODE ////////////////////
	mov psrsave=psr			// Disable IC (no PMIs)
	rsm psr.i | psr.dt | psr.ic;;
	srlz.i;;

	st8.rel [ptc0]=data0		// Write PTC0 & wait for completion.

5:	ld8.acq	scr1=[piowcphy];;	// Wait for PIOs to complete.
	and	scr2=scr1,mask;;	// mask of writecount bits
	cmp.ne	p6,p0=zeroval,scr2
(p6)	br.cond.sptk 5b;;

	tbit.nz	p8,p7=scr1,DEADLOCKBIT;;// Test for DEADLOCK
(p7)	cmp.ne p7,p0=r0,ptc1;;		// Test for non-null ptc1
	
(p7)	st8.rel [ptc1]=data1;;		// Now write PTC1.

5:	ld8.acq	scr1=[piowcphy];;	// Wait for PIOs to complete.
	and	scr2=scr1,mask;;	// mask of writecount bits
	cmp.ne	p6,p0=zeroval,scr2
(p6)	br.cond.sptk 5b
	
	tbit.nz	p8,p0=scr1,DEADLOCKBIT;;// Test for DEADLOCK

	mov psr.l=psrsave;;		// Reenable IC
	srlz.i;;
	////////////// END   PHYSICAL MODE ////////////////////

(p8)	br.cond.spnt 1b;;		// Repeat if DEADLOCK occurred.

	br.ret.sptk	rp
	.endp sn2_ptc_deadlock_recovery_core
