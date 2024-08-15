/* SPDX-License-Identifier: GPL-2.0 */
/*
 * File:        mca_drv_asm.S
 * Purpose:     Assembly portion of Generic MCA handling
 *
 * Copyright (C) 2004 FUJITSU LIMITED
 * Copyright (C) 2004 Hidetoshi Seto <seto.hidetoshi@jp.fujitsu.com>
 */
#include <linux/threads.h>

#include <asm/asmmacro.h>
#include <asm/processor.h>
#include <asm/ptrace.h>

GLOBAL_ENTRY(mca_handler_bhhook)
	invala				// clear RSE ?
	cover
	;;
	clrrrb
	;;						
	alloc	r16=ar.pfs,0,2,3,0	// make a new frame
	mov	ar.rsc=0
	mov	r13=IA64_KR(CURRENT)	// current task pointer
	;;
	mov	r2=r13
	;;
	addl	r22=IA64_RBS_OFFSET,r2
	;;
	mov	ar.bspstore=r22
	addl	sp=IA64_STK_OFFSET-IA64_PT_REGS_SIZE,r2
	;;
	adds	r2=IA64_TASK_THREAD_ON_USTACK_OFFSET,r13
	;;
	st1	[r2]=r0		// clear current->thread.on_ustack flag
	mov	loc0=r16
	movl	loc1=mca_handler_bh	// recovery C function
	;;
	mov	out0=r8			// poisoned address
	mov	out1=r9			// iip
	mov	out2=r10		// psr
	mov	b6=loc1
	;;
	mov	loc1=rp
	ssm	psr.ic
	;;
	srlz.i
	;;
	ssm	psr.i
	br.call.sptk.many rp=b6		// does not return ...
	;;
	mov	ar.pfs=loc0
	mov 	rp=loc1
	;;
	mov	r8=r0
	br.ret.sptk.many rp
END(mca_handler_bhhook)
