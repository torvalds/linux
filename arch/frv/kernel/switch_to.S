###############################################################################
#
# switch_to.S: context switch operation
#
# Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
# Written by David Howells (dhowells@redhat.com)
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version
# 2 of the License, or (at your option) any later version.
#
###############################################################################

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/processor.h>
#include <asm/registers.h>
#include <asm/spr-regs.h>

.macro LEDS val
	setlos		#~\val,gr27
	st		gr27,@(gr30,gr0)
	membar
	dcf		@(gr30,gr0)
.endm

	.section	.sdata
	.balign		8

	# address of frame 0 (userspace) on current kernel stack
	.globl		__kernel_frame0_ptr
__kernel_frame0_ptr:
	.long		init_thread_union + THREAD_SIZE - FRV_FRAME0_SIZE

	# address of current task
	.globl		__kernel_current_task
__kernel_current_task:
	.long		init_task

	.section	.text
	.balign		4

###############################################################################
#
# struct task_struct *__switch_to(struct thread_struct *prev_thread,
#				  struct thread_struct *next_thread,
#				  struct task_struct *prev)
#
###############################################################################
	.globl		__switch_to
__switch_to:
	# save outgoing process's context
	sethi.p		%hi(__switch_back),gr13
	setlo		%lo(__switch_back),gr13
	movsg		lr,gr12

	stdi		gr28,@(gr8,#__THREAD_FRAME)
	sti		sp  ,@(gr8,#__THREAD_SP)
	sti		fp  ,@(gr8,#__THREAD_FP)
	stdi		gr12,@(gr8,#__THREAD_LR)
	stdi		gr16,@(gr8,#__THREAD_GR(16))
	stdi		gr18,@(gr8,#__THREAD_GR(18))
	stdi		gr20,@(gr8,#__THREAD_GR(20))
	stdi		gr22,@(gr8,#__THREAD_GR(22))
	stdi		gr24,@(gr8,#__THREAD_GR(24))
	stdi.p		gr26,@(gr8,#__THREAD_GR(26))

	or		gr8,gr8,gr22
	ldi.p		@(gr8,#__THREAD_USER),gr8
	call		save_user_regs
	or		gr22,gr22,gr8
	
	# retrieve the new context
	sethi.p		%hi(__kernel_frame0_ptr),gr6
	setlo		%lo(__kernel_frame0_ptr),gr6
	movsg		psr,gr4

	lddi.p		@(gr9,#__THREAD_FRAME),gr10
	or		gr10,gr10,gr27		; save prev for the return value

	ldi		@(gr11,#4),gr19		; get new_current->thread_info

	lddi		@(gr9,#__THREAD_SP),gr12
	ldi		@(gr9,#__THREAD_LR),gr14
	ldi		@(gr9,#__THREAD_PC),gr18
	ldi.p		@(gr9,#__THREAD_FRAME0),gr7

	# actually switch kernel contexts with ordinary exceptions disabled
	andi		gr4,#~PSR_ET,gr5
	movgs		gr5,psr

	or.p		gr10,gr0,gr28		; set __frame
	or		gr11,gr0,gr29		; set __current
	or.p		gr12,gr0,sp
	or		gr13,gr0,fp
	or		gr19,gr0,gr15		; set __current_thread_info

	sti		gr7,@(gr6,#0)		; set __kernel_frame0_ptr
	sti		gr29,@(gr6,#4)		; set __kernel_current_task

	movgs		gr14,lr
	bar

	# jump to __switch_back or ret_from_fork as appropriate
	# - move prev to GR8
	movgs		gr4,psr
	jmpl.p		@(gr18,gr0)
	or		gr27,gr27,gr8

###############################################################################
#
# restore incoming process's context
# - on entry:
#   - SP, FP, LR, GR15, GR28 and GR29 will have been set up appropriately
#   - GR8 will point to the outgoing task_struct
#   - GR9 will point to the incoming thread_struct
#
###############################################################################
__switch_back:
	lddi		@(gr9,#__THREAD_GR(16)),gr16
	lddi		@(gr9,#__THREAD_GR(18)),gr18
	lddi		@(gr9,#__THREAD_GR(20)),gr20
	lddi		@(gr9,#__THREAD_GR(22)),gr22
	lddi		@(gr9,#__THREAD_GR(24)),gr24
	lddi		@(gr9,#__THREAD_GR(26)),gr26

	# fall through into restore_user_regs()
	ldi.p		@(gr9,#__THREAD_USER),gr8
	or		gr8,gr8,gr9

###############################################################################
#
# restore extra general regs and FP/Media regs
# - void *restore_user_regs(const struct user_context *target, void *retval)
# - on entry:
#   - GR8 will point to the user context to swap in
#   - GR9 will contain the value to be returned in GR8 (prev task on context switch)
#
###############################################################################
	.globl		restore_user_regs
restore_user_regs:
	movsg		hsr0,gr6
	ori		gr6,#HSR0_GRHE|HSR0_FRLE|HSR0_FRHE,gr6
	movgs		gr6,hsr0
	movsg		hsr0,gr6

	movsg		psr,gr7
	ori		gr7,#PSR_EF|PSR_EM,gr7
	movgs		gr7,psr
	movsg		psr,gr7
	srli		gr7,#24,gr7
	bar

	lddi		@(gr8,#__FPMEDIA_MSR(0)),gr4

	movgs		gr4,msr0
	movgs		gr5,msr1

	lddfi		@(gr8,#__FPMEDIA_ACC(0)),fr16
	lddfi		@(gr8,#__FPMEDIA_ACC(2)),fr18
	ldbfi		@(gr8,#__FPMEDIA_ACCG(0)),fr20
	ldbfi		@(gr8,#__FPMEDIA_ACCG(1)),fr21
	ldbfi		@(gr8,#__FPMEDIA_ACCG(2)),fr22
	ldbfi		@(gr8,#__FPMEDIA_ACCG(3)),fr23

	mwtacc		fr16,acc0
	mwtacc		fr17,acc1
	mwtacc		fr18,acc2
	mwtacc		fr19,acc3
	mwtaccg		fr20,accg0
	mwtaccg		fr21,accg1
	mwtaccg		fr22,accg2
	mwtaccg		fr23,accg3

	# some CPUs have extra ACCx and ACCGx regs and maybe FSRx regs
	subicc.p	gr7,#0x50,gr0,icc0
	subicc		gr7,#0x31,gr0,icc1
	beq		icc0,#0,__restore_acc_fr451
	beq		icc1,#0,__restore_acc_fr555
__restore_acc_cont:

	# some CPU's have GR32-GR63
	setlos		#HSR0_FRHE,gr4
	andcc		gr6,gr4,gr0,icc0
	beq		icc0,#1,__restore_skip_gr32_gr63

	lddi		@(gr8,#__INT_GR(32)),gr32
	lddi		@(gr8,#__INT_GR(34)),gr34
	lddi		@(gr8,#__INT_GR(36)),gr36
	lddi		@(gr8,#__INT_GR(38)),gr38
	lddi		@(gr8,#__INT_GR(40)),gr40
	lddi		@(gr8,#__INT_GR(42)),gr42
	lddi		@(gr8,#__INT_GR(44)),gr44
	lddi		@(gr8,#__INT_GR(46)),gr46
	lddi		@(gr8,#__INT_GR(48)),gr48
	lddi		@(gr8,#__INT_GR(50)),gr50
	lddi		@(gr8,#__INT_GR(52)),gr52
	lddi		@(gr8,#__INT_GR(54)),gr54
	lddi		@(gr8,#__INT_GR(56)),gr56
	lddi		@(gr8,#__INT_GR(58)),gr58
	lddi		@(gr8,#__INT_GR(60)),gr60
	lddi		@(gr8,#__INT_GR(62)),gr62
__restore_skip_gr32_gr63:

	# all CPU's have FR0-FR31
	lddfi		@(gr8,#__FPMEDIA_FR( 0)),fr0
	lddfi		@(gr8,#__FPMEDIA_FR( 2)),fr2
	lddfi		@(gr8,#__FPMEDIA_FR( 4)),fr4
	lddfi		@(gr8,#__FPMEDIA_FR( 6)),fr6
	lddfi		@(gr8,#__FPMEDIA_FR( 8)),fr8
	lddfi		@(gr8,#__FPMEDIA_FR(10)),fr10
	lddfi		@(gr8,#__FPMEDIA_FR(12)),fr12
	lddfi		@(gr8,#__FPMEDIA_FR(14)),fr14
	lddfi		@(gr8,#__FPMEDIA_FR(16)),fr16
	lddfi		@(gr8,#__FPMEDIA_FR(18)),fr18
	lddfi		@(gr8,#__FPMEDIA_FR(20)),fr20
	lddfi		@(gr8,#__FPMEDIA_FR(22)),fr22
	lddfi		@(gr8,#__FPMEDIA_FR(24)),fr24
	lddfi		@(gr8,#__FPMEDIA_FR(26)),fr26
	lddfi		@(gr8,#__FPMEDIA_FR(28)),fr28
	lddfi.p		@(gr8,#__FPMEDIA_FR(30)),fr30

	# some CPU's have FR32-FR63
	setlos		#HSR0_FRHE,gr4
	andcc		gr6,gr4,gr0,icc0
	beq		icc0,#1,__restore_skip_fr32_fr63

	lddfi		@(gr8,#__FPMEDIA_FR(32)),fr32
	lddfi		@(gr8,#__FPMEDIA_FR(34)),fr34
	lddfi		@(gr8,#__FPMEDIA_FR(36)),fr36
	lddfi		@(gr8,#__FPMEDIA_FR(38)),fr38
	lddfi		@(gr8,#__FPMEDIA_FR(40)),fr40
	lddfi		@(gr8,#__FPMEDIA_FR(42)),fr42
	lddfi		@(gr8,#__FPMEDIA_FR(44)),fr44
	lddfi		@(gr8,#__FPMEDIA_FR(46)),fr46
	lddfi		@(gr8,#__FPMEDIA_FR(48)),fr48
	lddfi		@(gr8,#__FPMEDIA_FR(50)),fr50
	lddfi		@(gr8,#__FPMEDIA_FR(52)),fr52
	lddfi		@(gr8,#__FPMEDIA_FR(54)),fr54
	lddfi		@(gr8,#__FPMEDIA_FR(56)),fr56
	lddfi		@(gr8,#__FPMEDIA_FR(58)),fr58
	lddfi		@(gr8,#__FPMEDIA_FR(60)),fr60
	lddfi		@(gr8,#__FPMEDIA_FR(62)),fr62
__restore_skip_fr32_fr63:

	lddi		@(gr8,#__FPMEDIA_FNER(0)),gr4
	movsg		fner0,gr4
	movsg		fner1,gr5
	or.p		gr9,gr9,gr8
	bralr

	# the FR451 also has ACC8-11/ACCG8-11 regs (but not 4-7...)
__restore_acc_fr451:
	lddfi		@(gr8,#__FPMEDIA_ACC(4)),fr16
	lddfi		@(gr8,#__FPMEDIA_ACC(6)),fr18
	ldbfi		@(gr8,#__FPMEDIA_ACCG(4)),fr20
	ldbfi		@(gr8,#__FPMEDIA_ACCG(5)),fr21
	ldbfi		@(gr8,#__FPMEDIA_ACCG(6)),fr22
	ldbfi		@(gr8,#__FPMEDIA_ACCG(7)),fr23

	mwtacc		fr16,acc8
	mwtacc		fr17,acc9
	mwtacc		fr18,acc10
	mwtacc		fr19,acc11
	mwtaccg		fr20,accg8
	mwtaccg		fr21,accg9
	mwtaccg		fr22,accg10
	mwtaccg		fr23,accg11
	bra		__restore_acc_cont

	# the FR555 also has ACC4-7/ACCG4-7 regs and an FSR0 reg
__restore_acc_fr555:
	lddfi		@(gr8,#__FPMEDIA_ACC(4)),fr16
	lddfi		@(gr8,#__FPMEDIA_ACC(6)),fr18
	ldbfi		@(gr8,#__FPMEDIA_ACCG(4)),fr20
	ldbfi		@(gr8,#__FPMEDIA_ACCG(5)),fr21
	ldbfi		@(gr8,#__FPMEDIA_ACCG(6)),fr22
	ldbfi		@(gr8,#__FPMEDIA_ACCG(7)),fr23

	mnop.p
	mwtacc		fr16,acc4
	mnop.p
	mwtacc		fr17,acc5
	mnop.p
	mwtacc		fr18,acc6
	mnop.p
	mwtacc		fr19,acc7
	mnop.p
	mwtaccg		fr20,accg4
	mnop.p
	mwtaccg		fr21,accg5
	mnop.p
	mwtaccg		fr22,accg6
	mnop.p
	mwtaccg		fr23,accg7

	ldi		@(gr8,#__FPMEDIA_FSR(0)),gr4
	movgs		gr4,fsr0

	bra		__restore_acc_cont


###############################################################################
#
# save extra general regs and FP/Media regs
# - void save_user_regs(struct user_context *target)
#
###############################################################################
	.globl		save_user_regs
save_user_regs:
	movsg		hsr0,gr6
	ori		gr6,#HSR0_GRHE|HSR0_FRLE|HSR0_FRHE,gr6
	movgs		gr6,hsr0
	movsg		hsr0,gr6

	movsg		psr,gr7
	ori		gr7,#PSR_EF|PSR_EM,gr7
	movgs		gr7,psr
	movsg		psr,gr7
	srli		gr7,#24,gr7
	bar

	movsg		fner0,gr4
	movsg		fner1,gr5
	stdi.p		gr4,@(gr8,#__FPMEDIA_FNER(0))

	# some CPU's have GR32-GR63
	setlos		#HSR0_GRHE,gr4
	andcc		gr6,gr4,gr0,icc0
	beq		icc0,#1,__save_skip_gr32_gr63

	stdi		gr32,@(gr8,#__INT_GR(32))
	stdi		gr34,@(gr8,#__INT_GR(34))
	stdi		gr36,@(gr8,#__INT_GR(36))
	stdi		gr38,@(gr8,#__INT_GR(38))
	stdi		gr40,@(gr8,#__INT_GR(40))
	stdi		gr42,@(gr8,#__INT_GR(42))
	stdi		gr44,@(gr8,#__INT_GR(44))
	stdi		gr46,@(gr8,#__INT_GR(46))
	stdi		gr48,@(gr8,#__INT_GR(48))
	stdi		gr50,@(gr8,#__INT_GR(50))
	stdi		gr52,@(gr8,#__INT_GR(52))
	stdi		gr54,@(gr8,#__INT_GR(54))
	stdi		gr56,@(gr8,#__INT_GR(56))
	stdi		gr58,@(gr8,#__INT_GR(58))
	stdi		gr60,@(gr8,#__INT_GR(60))
	stdi		gr62,@(gr8,#__INT_GR(62))
__save_skip_gr32_gr63:

	# all CPU's have FR0-FR31
	stdfi		fr0 ,@(gr8,#__FPMEDIA_FR( 0))
	stdfi		fr2 ,@(gr8,#__FPMEDIA_FR( 2))
	stdfi		fr4 ,@(gr8,#__FPMEDIA_FR( 4))
	stdfi		fr6 ,@(gr8,#__FPMEDIA_FR( 6))
	stdfi		fr8 ,@(gr8,#__FPMEDIA_FR( 8))
	stdfi		fr10,@(gr8,#__FPMEDIA_FR(10))
	stdfi		fr12,@(gr8,#__FPMEDIA_FR(12))
	stdfi		fr14,@(gr8,#__FPMEDIA_FR(14))
	stdfi		fr16,@(gr8,#__FPMEDIA_FR(16))
	stdfi		fr18,@(gr8,#__FPMEDIA_FR(18))
	stdfi		fr20,@(gr8,#__FPMEDIA_FR(20))
	stdfi		fr22,@(gr8,#__FPMEDIA_FR(22))
	stdfi		fr24,@(gr8,#__FPMEDIA_FR(24))
	stdfi		fr26,@(gr8,#__FPMEDIA_FR(26))
	stdfi		fr28,@(gr8,#__FPMEDIA_FR(28))
	stdfi.p		fr30,@(gr8,#__FPMEDIA_FR(30))

	# some CPU's have FR32-FR63
	setlos		#HSR0_FRHE,gr4
	andcc		gr6,gr4,gr0,icc0
	beq		icc0,#1,__save_skip_fr32_fr63

	stdfi		fr32,@(gr8,#__FPMEDIA_FR(32))
	stdfi		fr34,@(gr8,#__FPMEDIA_FR(34))
	stdfi		fr36,@(gr8,#__FPMEDIA_FR(36))
	stdfi		fr38,@(gr8,#__FPMEDIA_FR(38))
	stdfi		fr40,@(gr8,#__FPMEDIA_FR(40))
	stdfi		fr42,@(gr8,#__FPMEDIA_FR(42))
	stdfi		fr44,@(gr8,#__FPMEDIA_FR(44))
	stdfi		fr46,@(gr8,#__FPMEDIA_FR(46))
	stdfi		fr48,@(gr8,#__FPMEDIA_FR(48))
	stdfi		fr50,@(gr8,#__FPMEDIA_FR(50))
	stdfi		fr52,@(gr8,#__FPMEDIA_FR(52))
	stdfi		fr54,@(gr8,#__FPMEDIA_FR(54))
	stdfi		fr56,@(gr8,#__FPMEDIA_FR(56))
	stdfi		fr58,@(gr8,#__FPMEDIA_FR(58))
	stdfi		fr60,@(gr8,#__FPMEDIA_FR(60))
	stdfi		fr62,@(gr8,#__FPMEDIA_FR(62))
__save_skip_fr32_fr63:

	mrdacc		acc0 ,fr4
	mrdacc		acc1 ,fr5

	stdfi.p		fr4 ,@(gr8,#__FPMEDIA_ACC(0))

	mrdacc		acc2 ,fr6
	mrdacc		acc3 ,fr7

	stdfi.p		fr6 ,@(gr8,#__FPMEDIA_ACC(2))

	mrdaccg		accg0,fr4
	stbfi.p		fr4 ,@(gr8,#__FPMEDIA_ACCG(0))

	mrdaccg		accg1,fr5
	stbfi.p		fr5 ,@(gr8,#__FPMEDIA_ACCG(1))

	mrdaccg		accg2,fr6
	stbfi.p		fr6 ,@(gr8,#__FPMEDIA_ACCG(2))

	mrdaccg		accg3,fr7
	stbfi		fr7 ,@(gr8,#__FPMEDIA_ACCG(3))

	movsg		msr0 ,gr4
	movsg		msr1 ,gr5

	stdi		gr4 ,@(gr8,#__FPMEDIA_MSR(0))

	# some CPUs have extra ACCx and ACCGx regs and maybe FSRx regs
	subicc.p	gr7,#0x50,gr0,icc0
	subicc		gr7,#0x31,gr0,icc1
	beq		icc0,#0,__save_acc_fr451
	beq		icc1,#0,__save_acc_fr555
__save_acc_cont:

	lddfi		@(gr8,#__FPMEDIA_FR(4)),fr4
	lddfi.p		@(gr8,#__FPMEDIA_FR(6)),fr6
	bralr

	# the FR451 also has ACC8-11/ACCG8-11 regs (but not 4-7...)
__save_acc_fr451:
	mrdacc		acc8 ,fr4
	mrdacc		acc9 ,fr5

	stdfi.p		fr4 ,@(gr8,#__FPMEDIA_ACC(4))

	mrdacc		acc10,fr6
	mrdacc		acc11,fr7

	stdfi.p		fr6 ,@(gr8,#__FPMEDIA_ACC(6))

	mrdaccg		accg8,fr4
	stbfi.p		fr4 ,@(gr8,#__FPMEDIA_ACCG(4))

	mrdaccg		accg9,fr5
	stbfi.p		fr5 ,@(gr8,#__FPMEDIA_ACCG(5))

	mrdaccg		accg10,fr6
	stbfi.p		fr6 ,@(gr8,#__FPMEDIA_ACCG(6))

	mrdaccg		accg11,fr7
	stbfi		fr7 ,@(gr8,#__FPMEDIA_ACCG(7))
	bra		__save_acc_cont

	# the FR555 also has ACC4-7/ACCG4-7 regs and an FSR0 reg
__save_acc_fr555:
	mnop.p
	mrdacc		acc4 ,fr4
	mnop.p
	mrdacc		acc5 ,fr5

	stdfi		fr4 ,@(gr8,#__FPMEDIA_ACC(4))

	mnop.p
	mrdacc		acc6 ,fr6
	mnop.p
	mrdacc		acc7 ,fr7

	stdfi		fr6 ,@(gr8,#__FPMEDIA_ACC(6))

	mnop.p
	mrdaccg		accg4,fr4
	stbfi		fr4 ,@(gr8,#__FPMEDIA_ACCG(4))

	mnop.p
	mrdaccg		accg5,fr5
	stbfi		fr5 ,@(gr8,#__FPMEDIA_ACCG(5))

	mnop.p
	mrdaccg		accg6,fr6
	stbfi		fr6 ,@(gr8,#__FPMEDIA_ACCG(6))

	mnop.p
	mrdaccg		accg7,fr7
	stbfi		fr7 ,@(gr8,#__FPMEDIA_ACCG(7))

	movsg		fsr0 ,gr4
	sti		gr4 ,@(gr8,#__FPMEDIA_FSR(0))
	bra		__save_acc_cont
