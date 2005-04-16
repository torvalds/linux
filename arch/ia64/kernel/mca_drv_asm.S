/*
 * File:        mca_drv_asm.S
 * Purpose:     Assembly portion of Generic MCA handling
 *
 * Copyright (C) 2004 FUJITSU LIMITED
 * Copyright (C) Hidetoshi Seto (seto.hidetoshi@jp.fujitsu.com)
 */
#include <linux/config.h>
#include <linux/threads.h>

#include <asm/asmmacro.h>
#include <asm/processor.h>

GLOBAL_ENTRY(mca_handler_bhhook)
	invala						// clear RSE ?
	;;						//
	cover						// 
	;;						//
	clrrrb						//
	;;						
	alloc		r16=ar.pfs,0,2,1,0		// make a new frame
	;;
	mov		r13=IA64_KR(CURRENT)		// current task pointer
	;;
	adds		r12=IA64_TASK_THREAD_KSP_OFFSET,r13
	;;
	ld8		r12=[r12]			// stack pointer
	;;
	mov		loc0=r16
	movl		loc1=mca_handler_bh		// recovery C function
	;;
	mov		out0=r8				// poisoned address
	mov		b6=loc1
	;;
	mov		loc1=rp
	;;
	br.call.sptk.many    rp=b6			// not return ...
	;;
	mov		ar.pfs=loc0
	mov 		rp=loc1
	;;
	mov		r8=r0
	br.ret.sptk.many rp
	;;
END(mca_handler_bhhook)
