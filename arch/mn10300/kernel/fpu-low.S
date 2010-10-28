/* MN10300 Low level FPU management operations
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/linkage.h>
#include <asm/cpu-regs.h>
#include <asm/smp.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/frame.inc>

.macro FPU_INIT_STATE_ALL
	fmov	0,fs0
	fmov	fs0,fs1
	fmov	fs0,fs2
	fmov	fs0,fs3
	fmov	fs0,fs4
	fmov	fs0,fs5
	fmov	fs0,fs6
	fmov	fs0,fs7
	fmov	fs0,fs8
	fmov	fs0,fs9
	fmov	fs0,fs10
	fmov	fs0,fs11
	fmov	fs0,fs12
	fmov	fs0,fs13
	fmov	fs0,fs14
	fmov	fs0,fs15
	fmov	fs0,fs16
	fmov	fs0,fs17
	fmov	fs0,fs18
	fmov	fs0,fs19
	fmov	fs0,fs20
	fmov	fs0,fs21
	fmov	fs0,fs22
	fmov	fs0,fs23
	fmov	fs0,fs24
	fmov	fs0,fs25
	fmov	fs0,fs26
	fmov	fs0,fs27
	fmov	fs0,fs28
	fmov	fs0,fs29
	fmov	fs0,fs30
	fmov	fs0,fs31
	fmov	FPCR_INIT,fpcr
.endm

.macro FPU_SAVE_ALL areg,dreg
	fmov	fs0,(\areg+)
	fmov	fs1,(\areg+)
	fmov	fs2,(\areg+)
	fmov	fs3,(\areg+)
	fmov	fs4,(\areg+)
	fmov	fs5,(\areg+)
	fmov	fs6,(\areg+)
	fmov	fs7,(\areg+)
	fmov	fs8,(\areg+)
	fmov	fs9,(\areg+)
	fmov	fs10,(\areg+)
	fmov	fs11,(\areg+)
	fmov	fs12,(\areg+)
	fmov	fs13,(\areg+)
	fmov	fs14,(\areg+)
	fmov	fs15,(\areg+)
	fmov	fs16,(\areg+)
	fmov	fs17,(\areg+)
	fmov	fs18,(\areg+)
	fmov	fs19,(\areg+)
	fmov	fs20,(\areg+)
	fmov	fs21,(\areg+)
	fmov	fs22,(\areg+)
	fmov	fs23,(\areg+)
	fmov	fs24,(\areg+)
	fmov	fs25,(\areg+)
	fmov	fs26,(\areg+)
	fmov	fs27,(\areg+)
	fmov	fs28,(\areg+)
	fmov	fs29,(\areg+)
	fmov	fs30,(\areg+)
	fmov	fs31,(\areg+)
	fmov	fpcr,\dreg
	mov	\dreg,(\areg)
.endm

.macro FPU_RESTORE_ALL areg,dreg
	fmov	(\areg+),fs0
	fmov	(\areg+),fs1
	fmov	(\areg+),fs2
	fmov	(\areg+),fs3
	fmov	(\areg+),fs4
	fmov	(\areg+),fs5
	fmov	(\areg+),fs6
	fmov	(\areg+),fs7
	fmov	(\areg+),fs8
	fmov	(\areg+),fs9
	fmov	(\areg+),fs10
	fmov	(\areg+),fs11
	fmov	(\areg+),fs12
	fmov	(\areg+),fs13
	fmov	(\areg+),fs14
	fmov	(\areg+),fs15
	fmov	(\areg+),fs16
	fmov	(\areg+),fs17
	fmov	(\areg+),fs18
	fmov	(\areg+),fs19
	fmov	(\areg+),fs20
	fmov	(\areg+),fs21
	fmov	(\areg+),fs22
	fmov	(\areg+),fs23
	fmov	(\areg+),fs24
	fmov	(\areg+),fs25
	fmov	(\areg+),fs26
	fmov	(\areg+),fs27
	fmov	(\areg+),fs28
	fmov	(\areg+),fs29
	fmov	(\areg+),fs30
	fmov	(\areg+),fs31
	mov	(\areg),\dreg
	fmov	\dreg,fpcr
.endm

###############################################################################
#
# void fpu_init_state(void)
# - initialise the FPU
#
###############################################################################
	.globl	fpu_init_state
	.type	fpu_init_state,@function
fpu_init_state:
	mov	epsw,d0
	or	EPSW_FE,epsw

#ifdef CONFIG_MN10300_PROC_MN103E010
	nop
	nop
	nop
#endif
	FPU_INIT_STATE_ALL
#ifdef CONFIG_MN10300_PROC_MN103E010
	nop
	nop
	nop
#endif
	mov	d0,epsw
	ret	[],0

	.size	fpu_init_state,.-fpu_init_state

###############################################################################
#
# void fpu_save(struct fpu_state_struct *)
# - save the fpu state
# - note that an FPU Operational exception might occur during this process
#
###############################################################################
	.globl	fpu_save
	.type	fpu_save,@function
fpu_save:
	mov	epsw,d1
	or	EPSW_FE,epsw		/* enable the FPU so we can access it */

#ifdef CONFIG_MN10300_PROC_MN103E010
	nop
	nop
#endif
	mov	d0,a0
	FPU_SAVE_ALL	a0,d0
#ifdef CONFIG_MN10300_PROC_MN103E010
	nop
	nop
#endif

	mov	d1,epsw
	ret	[],0

	.size	fpu_save,.-fpu_save

###############################################################################
#
# void fpu_disabled(void)
# - handle an exception due to the FPU being disabled
#   when CONFIG_FPU is enabled
#
###############################################################################
	.type	fpu_disabled,@function
	.globl	fpu_disabled
fpu_disabled:
	or	EPSW_nAR|EPSW_FE,epsw
	nop
	nop
	nop

	mov	sp,a1
	mov	(a1),d1			/* get epsw of user context */
	and	~(THREAD_SIZE-1),a1	/* a1: (thread_info *ti) */
	mov	(TI_task,a1),a2		/* a2: (task_struct *tsk) */
	btst	EPSW_nSL,d1
	beq	fpu_used_in_kernel

	or	EPSW_FE,d1
	mov	d1,(sp)
	mov	(TASK_THREAD+THREAD_FPU_FLAGS,a2),d1
#ifndef CONFIG_LAZY_SAVE_FPU
	or	__THREAD_HAS_FPU,d1
	mov	d1,(TASK_THREAD+THREAD_FPU_FLAGS,a2)
#else  /* !CONFIG_LAZY_SAVE_FPU */
	mov	(fpu_state_owner),a0
	cmp	0,a0
	beq	fpu_regs_save_end

	mov	(TASK_THREAD+THREAD_UREGS,a0),a1
	add	TASK_THREAD+THREAD_FPU_STATE,a0
	FPU_SAVE_ALL a0,d0

	mov	(REG_EPSW,a1),d0
	and	~EPSW_FE,d0
	mov	d0,(REG_EPSW,a1)

fpu_regs_save_end:
	mov	a2,(fpu_state_owner)
#endif /* !CONFIG_LAZY_SAVE_FPU */

	btst	__THREAD_USING_FPU,d1
	beq	fpu_regs_init
	add	TASK_THREAD+THREAD_FPU_STATE,a2
	FPU_RESTORE_ALL a2,d0
	rti

fpu_regs_init:
	FPU_INIT_STATE_ALL
	add	TASK_THREAD+THREAD_FPU_FLAGS,a2
	bset	__THREAD_USING_FPU,(0,a2)
	rti

fpu_used_in_kernel:
	and	~(EPSW_nAR|EPSW_FE),epsw
	nop
	nop

	add	-4,sp
	SAVE_ALL
	mov	-1,d0
	mov	d0,(REG_ORIG_D0,fp)

	and	~EPSW_NMID,epsw

	mov	fp,d0
	call	fpu_disabled_in_kernel[],0
	jmp	ret_from_exception

	.size	fpu_disabled,.-fpu_disabled
