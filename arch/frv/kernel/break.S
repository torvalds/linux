/* break.S: Break interrupt handling (kept separate from entry.S)
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/linkage.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/ptrace.h>
#include <asm/thread_info.h>
#include <asm/spr-regs.h>

#include <asm/errno.h>

#
# the break handler has its own stack
#
	.section	.bss.stack
	.globl		__break_user_context
	.balign		THREAD_SIZE
__break_stack:
	.space		THREAD_SIZE - FRV_FRAME0_SIZE
__break_frame_0:
	.space		FRV_FRAME0_SIZE

#
# miscellaneous variables
#
	.section	.bss
#ifdef CONFIG_MMU
	.globl		__break_tlb_miss_real_return_info
__break_tlb_miss_real_return_info:
	.balign		8
	.space		2*4			/* saved PCSR, PSR for TLB-miss handler fixup */
#endif

__break_trace_through_exceptions:
	.space		4

#define CS2_ECS1	0xe1200000
#define CS2_USERLED	0x4

.macro LEDS val,reg
#	sethi.p		%hi(CS2_ECS1+CS2_USERLED),gr30
#	setlo		%lo(CS2_ECS1+CS2_USERLED),gr30
#	setlos		#~\val,\reg
#	st		\reg,@(gr30,gr0)
#	setlos		#0x5555,\reg
#	sethi.p		%hi(0xffc00100),gr30
#	setlo		%lo(0xffc00100),gr30
#	sth		\reg,@(gr30,gr0)
#	membar
.endm

###############################################################################
#
# entry point for Break Exceptions/Interrupts
#
###############################################################################
	.text
	.balign		4
	.globl		__entry_break
__entry_break:
#ifdef CONFIG_MMU
	movgs		gr31,scr3
#endif
	LEDS		0x1001,gr31

	sethi.p		%hi(__break_frame_0),gr31
	setlo		%lo(__break_frame_0),gr31

	stdi		gr2,@(gr31,#REG_GR(2))
	movsg		ccr,gr3
	sti		gr3,@(gr31,#REG_CCR)

	# catch the return from a TLB-miss handler that had single-step disabled
	# traps will be enabled, so we have to do this now
#ifdef CONFIG_MMU
	movsg		bpcsr,gr3
	sethi.p		%hi(__break_tlb_miss_return_breaks_here),gr2
	setlo		%lo(__break_tlb_miss_return_breaks_here),gr2
	subcc		gr2,gr3,gr0,icc0
	beq		icc0,#2,__break_return_singlestep_tlbmiss
#endif

	# determine whether we have stepped through into an exception
	# - we need to take special action to suspend h/w single stepping if we've done
	#   that, so that the gdbstub doesn't get bogged down endlessly stepping through
	#   external interrupt handling
	movsg		bpsr,gr3
	andicc		gr3,#BPSR_BET,gr0,icc0
	bne		icc0,#2,__break_maybe_userspace	/* jump if PSR.ET was 1 */

	LEDS		0x1003,gr2

	movsg		brr,gr3
	andicc		gr3,#BRR_ST,gr0,icc0
	andicc.p	gr3,#BRR_SB,gr0,icc1
	bne		icc0,#2,__break_step		/* jump if single-step caused break */
	beq		icc1,#2,__break_continue	/* jump if BREAK didn't cause break */

	LEDS		0x1007,gr2

	# handle special breaks
	movsg		bpcsr,gr3

	sethi.p		%hi(__entry_return_singlestep_breaks_here),gr2
	setlo		%lo(__entry_return_singlestep_breaks_here),gr2
	subcc		gr2,gr3,gr0,icc0
	beq		icc0,#2,__break_return_singlestep

	bra		__break_continue


###############################################################################
#
# handle BREAK instruction in kernel-mode exception epilogue
#
###############################################################################
__break_return_singlestep:
	LEDS		0x100f,gr2

	# special break insn requests single-stepping to be turned back on
	#		HERE		RETT
	# PSR.ET	0		0
	# PSR.PS	old PSR.S	?
	# PSR.S		1		1
	# BPSR.ET	0		1 (can't have caused orig excep otherwise)
	# BPSR.BS	1		old PSR.S
	movsg		dcr,gr2
	sethi.p		%hi(DCR_SE),gr3
	setlo		%lo(DCR_SE),gr3
	or		gr2,gr3,gr2
	movgs		gr2,dcr

	movsg		psr,gr2
	andi		gr2,#PSR_PS,gr2
	slli		gr2,#11,gr2			/* PSR.PS -> BPSR.BS */
	ori		gr2,#BPSR_BET,gr2		/* 1 -> BPSR.BET */
	movgs		gr2,bpsr

	# return to the invoker of the original kernel exception
	movsg		pcsr,gr2
	movgs		gr2,bpcsr

	LEDS		0x101f,gr2

	ldi		@(gr31,#REG_CCR),gr3
	movgs		gr3,ccr
	lddi.p		@(gr31,#REG_GR(2)),gr2
	xor		gr31,gr31,gr31
	movgs		gr0,brr
#ifdef CONFIG_MMU
	movsg		scr3,gr31
#endif
	rett		#1

###############################################################################
#
# handle BREAK instruction in TLB-miss handler return path
#
###############################################################################
#ifdef CONFIG_MMU
__break_return_singlestep_tlbmiss:
	LEDS		0x1100,gr2

	sethi.p		%hi(__break_tlb_miss_real_return_info),gr3
	setlo		%lo(__break_tlb_miss_real_return_info),gr3
	lddi		@(gr3,#0),gr2
	movgs		gr2,pcsr
	movgs		gr3,psr

	bra		__break_return_singlestep
#endif


###############################################################################
#
# handle single stepping into an exception prologue from kernel mode
# - we try and catch it whilst it is still in the main vector table
# - if we catch it there, we have to jump to the fixup handler
#   - there is a fixup table that has a pointer for every 16b slot in the trap
#     table
#
###############################################################################
__break_step:
	LEDS		0x2003,gr2

	# external interrupts seem to escape from the trap table before single
	# step catches up with them
	movsg		bpcsr,gr2
	sethi.p		%hi(__entry_kernel_external_interrupt),gr3
	setlo		%lo(__entry_kernel_external_interrupt),gr3
	subcc.p		gr2,gr3,gr0,icc0
	sethi		%hi(__entry_uspace_external_interrupt),gr3
	setlo.p		%lo(__entry_uspace_external_interrupt),gr3
	beq		icc0,#2,__break_step_kernel_external_interrupt
	subcc.p		gr2,gr3,gr0,icc0
	sethi		%hi(__entry_kernel_external_interrupt_virtually_disabled),gr3
	setlo.p		%lo(__entry_kernel_external_interrupt_virtually_disabled),gr3
	beq		icc0,#2,__break_step_uspace_external_interrupt
	subcc.p		gr2,gr3,gr0,icc0
	sethi		%hi(__entry_kernel_external_interrupt_virtual_reenable),gr3
	setlo.p		%lo(__entry_kernel_external_interrupt_virtual_reenable),gr3
	beq		icc0,#2,__break_step_kernel_external_interrupt_virtually_disabled
	subcc		gr2,gr3,gr0,icc0
	beq		icc0,#2,__break_step_kernel_external_interrupt_virtual_reenable

	LEDS		0x2007,gr2

	# the two main vector tables are adjacent on one 8Kb slab
	movsg		bpcsr,gr2
	setlos		#0xffffe000,gr3
	and		gr2,gr3,gr2
	sethi.p		%hi(__trap_tables),gr3
	setlo		%lo(__trap_tables),gr3
	subcc		gr2,gr3,gr0,icc0
	bne		icc0,#2,__break_continue

	LEDS		0x200f,gr2

	# skip workaround if so requested by GDB
	sethi.p		%hi(__break_trace_through_exceptions),gr3
	setlo		%lo(__break_trace_through_exceptions),gr3
	ld		@(gr3,gr0),gr3
	subcc		gr3,gr0,gr0,icc0
	bne		icc0,#0,__break_continue

	LEDS		0x201f,gr2

	# access the fixup table - there's a 1:1 mapping between the slots in the trap tables and
	# the slots in the trap fixup tables allowing us to simply divide the offset into the
	# former by 4 to access the latter
	sethi.p		%hi(__trap_tables),gr3
	setlo		%lo(__trap_tables),gr3
	movsg		bpcsr,gr2
	sub		gr2,gr3,gr2
	srli.p		gr2,#2,gr2

	sethi		%hi(__trap_fixup_tables),gr3
	setlo.p		%lo(__trap_fixup_tables),gr3
	andi		gr2,#~3,gr2
	ld		@(gr2,gr3),gr2
	jmpil		@(gr2,#0)

# step through an internal exception from kernel mode
	.globl		__break_step_kernel_softprog_interrupt
__break_step_kernel_softprog_interrupt:
	sethi.p		%hi(__entry_kernel_softprog_interrupt_reentry),gr3
	setlo		%lo(__entry_kernel_softprog_interrupt_reentry),gr3
	bra		__break_return_as_kernel_prologue

# step through an external interrupt from kernel mode
	.globl		__break_step_kernel_external_interrupt
__break_step_kernel_external_interrupt:
	# deal with virtual interrupt disablement
	beq		icc2,#0,__break_step_kernel_external_interrupt_virtually_disabled

	sethi.p		%hi(__entry_kernel_external_interrupt_reentry),gr3
	setlo		%lo(__entry_kernel_external_interrupt_reentry),gr3

__break_return_as_kernel_prologue:
	LEDS		0x203f,gr2

	movgs		gr3,bpcsr

	# do the bit we had to skip
#ifdef CONFIG_MMU
	movsg		ear0,gr2		/* EAR0 can get clobbered by gdb-stub (ICI/ICEI) */
	movgs		gr2,scr2
#endif

	or.p		sp,gr0,gr2		/* set up the stack pointer */
	subi		sp,#REG__END,sp
	sti.p		gr2,@(sp,#REG_SP)

	setlos		#REG__STATUS_STEP,gr2
	sti		gr2,@(sp,#REG__STATUS)		/* record single step status */

	# cancel single-stepping mode
	movsg		dcr,gr2
	sethi.p		%hi(~DCR_SE),gr3
	setlo		%lo(~DCR_SE),gr3
	and		gr2,gr3,gr2
	movgs		gr2,dcr

	LEDS		0x207f,gr2

	ldi		@(gr31,#REG_CCR),gr3
	movgs		gr3,ccr
	lddi.p		@(gr31,#REG_GR(2)),gr2
	xor		gr31,gr31,gr31
	movgs		gr0,brr
#ifdef CONFIG_MMU
	movsg		scr3,gr31
#endif
	rett		#1

# we single-stepped into an interrupt handler whilst interrupts were merely virtually disabled
# need to really disable interrupts, set flag, fix up and return
__break_step_kernel_external_interrupt_virtually_disabled:
	movsg		psr,gr2
	andi		gr2,#~PSR_PIL,gr2
	ori		gr2,#PSR_PIL_14,gr2	/* debugging interrupts only */
	movgs		gr2,psr

	ldi		@(gr31,#REG_CCR),gr3
	movgs		gr3,ccr
	subcc.p		gr0,gr0,gr0,icc2	/* leave Z set, clear C */

	# exceptions must've been enabled and we must've been in supervisor mode
	setlos		BPSR_BET|BPSR_BS,gr3
	movgs		gr3,bpsr

	# return to where the interrupt happened
	movsg		pcsr,gr2
	movgs		gr2,bpcsr

	lddi.p		@(gr31,#REG_GR(2)),gr2

	xor		gr31,gr31,gr31
	movgs		gr0,brr
#ifdef CONFIG_MMU
	movsg		scr3,gr31
#endif
	rett		#1

# we stepped through into the virtual interrupt reenablement trap
#
# we also want to single step anyway, but after fixing up so that we get an event on the
# instruction after the broken-into exception returns
	.globl		__break_step_kernel_external_interrupt_virtual_reenable
__break_step_kernel_external_interrupt_virtual_reenable:
	movsg		psr,gr2
	andi		gr2,#~PSR_PIL,gr2
	movgs		gr2,psr

	ldi		@(gr31,#REG_CCR),gr3
	movgs		gr3,ccr
	subicc		gr0,#1,gr0,icc2		/* clear Z, set C */

	# save the adjusted ICC2
	movsg		ccr,gr3
	sti		gr3,@(gr31,#REG_CCR)

	# exceptions must've been enabled and we must've been in supervisor mode
	setlos		BPSR_BET|BPSR_BS,gr3
	movgs		gr3,bpsr

	# return to where the trap happened
	movsg		pcsr,gr2
	movgs		gr2,bpcsr

	# and then process the single step
	bra		__break_continue

# step through an internal exception from uspace mode
	.globl		__break_step_uspace_softprog_interrupt
__break_step_uspace_softprog_interrupt:
	sethi.p		%hi(__entry_uspace_softprog_interrupt_reentry),gr3
	setlo		%lo(__entry_uspace_softprog_interrupt_reentry),gr3
	bra		__break_return_as_uspace_prologue

# step through an external interrupt from kernel mode
	.globl		__break_step_uspace_external_interrupt
__break_step_uspace_external_interrupt:
	sethi.p		%hi(__entry_uspace_external_interrupt_reentry),gr3
	setlo		%lo(__entry_uspace_external_interrupt_reentry),gr3

__break_return_as_uspace_prologue:
	LEDS		0x20ff,gr2

	movgs		gr3,bpcsr

	# do the bit we had to skip
	sethi.p		%hi(__kernel_frame0_ptr),gr28
	setlo		%lo(__kernel_frame0_ptr),gr28
	ldi.p		@(gr28,#0),gr28

	setlos		#REG__STATUS_STEP,gr2
	sti		gr2,@(gr28,#REG__STATUS)	/* record single step status */

	# cancel single-stepping mode
	movsg		dcr,gr2
	sethi.p		%hi(~DCR_SE),gr3
	setlo		%lo(~DCR_SE),gr3
	and		gr2,gr3,gr2
	movgs		gr2,dcr

	LEDS		0x20fe,gr2

	ldi		@(gr31,#REG_CCR),gr3
	movgs		gr3,ccr
	lddi.p		@(gr31,#REG_GR(2)),gr2
	xor		gr31,gr31,gr31
	movgs		gr0,brr
#ifdef CONFIG_MMU
	movsg		scr3,gr31
#endif
	rett		#1

#ifdef CONFIG_MMU
# step through an ITLB-miss handler from user mode
	.globl		__break_user_insn_tlb_miss
__break_user_insn_tlb_miss:
	# we'll want to try the trap stub again
	sethi.p		%hi(__trap_user_insn_tlb_miss),gr2
	setlo		%lo(__trap_user_insn_tlb_miss),gr2
	movgs		gr2,bpcsr

__break_tlb_miss_common:
	LEDS		0x2101,gr2

	# cancel single-stepping mode
	movsg		dcr,gr2
	sethi.p		%hi(~DCR_SE),gr3
	setlo		%lo(~DCR_SE),gr3
	and		gr2,gr3,gr2
	movgs		gr2,dcr

	# we'll swap the real return address for one with a BREAK insn so that we can re-enable
	# single stepping on return
	movsg		pcsr,gr2
	sethi.p		%hi(__break_tlb_miss_real_return_info),gr3
	setlo		%lo(__break_tlb_miss_real_return_info),gr3
	sti		gr2,@(gr3,#0)

	sethi.p		%hi(__break_tlb_miss_return_break),gr2
	setlo		%lo(__break_tlb_miss_return_break),gr2
	movgs		gr2,pcsr

	# we also have to fudge PSR because the return BREAK is in kernel space and we want
	# to get a BREAK fault not an access violation should the return be to userspace
	movsg		psr,gr2
	sti.p		gr2,@(gr3,#4)
	ori		gr2,#PSR_PS,gr2
	movgs		gr2,psr

	LEDS		0x2102,gr2

	ldi		@(gr31,#REG_CCR),gr3
	movgs		gr3,ccr
	lddi		@(gr31,#REG_GR(2)),gr2
	movsg		scr3,gr31
	movgs		gr0,brr
	rett		#1

# step through a DTLB-miss handler from user mode
	.globl		__break_user_data_tlb_miss
__break_user_data_tlb_miss:
	# we'll want to try the trap stub again
	sethi.p		%hi(__trap_user_data_tlb_miss),gr2
	setlo		%lo(__trap_user_data_tlb_miss),gr2
	movgs		gr2,bpcsr
	bra		__break_tlb_miss_common

# step through an ITLB-miss handler from kernel mode
	.globl		__break_kernel_insn_tlb_miss
__break_kernel_insn_tlb_miss:
	# we'll want to try the trap stub again
	sethi.p		%hi(__trap_kernel_insn_tlb_miss),gr2
	setlo		%lo(__trap_kernel_insn_tlb_miss),gr2
	movgs		gr2,bpcsr
	bra		__break_tlb_miss_common

# step through a DTLB-miss handler from kernel mode
	.globl		__break_kernel_data_tlb_miss
__break_kernel_data_tlb_miss:
	# we'll want to try the trap stub again
	sethi.p		%hi(__trap_kernel_data_tlb_miss),gr2
	setlo		%lo(__trap_kernel_data_tlb_miss),gr2
	movgs		gr2,bpcsr
	bra		__break_tlb_miss_common
#endif

###############################################################################
#
# handle debug events originating with userspace
#
###############################################################################
__break_maybe_userspace:
	LEDS		0x3003,gr2

	setlos		#BPSR_BS,gr2
	andcc		gr3,gr2,gr0,icc0
	bne		icc0,#0,__break_continue	/* skip if PSR.S was 1 */

	movsg		brr,gr2
	andicc		gr2,#BRR_ST|BRR_SB,gr0,icc0
	beq		icc0,#0,__break_continue	/* jump if not BREAK or single-step */

	LEDS		0x3007,gr2

	# do the first part of the exception prologue here
	sethi.p		%hi(__kernel_frame0_ptr),gr28
	setlo		%lo(__kernel_frame0_ptr),gr28
	ldi		@(gr28,#0),gr28
	andi		gr28,#~7,gr28

	# set up the kernel stack pointer
	sti		sp  ,@(gr28,#REG_SP)
	ori		gr28,0,sp
	sti		gr0 ,@(gr28,#REG_GR(28))

	stdi		gr20,@(gr28,#REG_GR(20))
	stdi		gr22,@(gr28,#REG_GR(22))

	movsg		tbr,gr20
	movsg		bpcsr,gr21
	movsg		psr,gr22

	# determine the exception type and cancel single-stepping mode
	or		gr0,gr0,gr23

	movsg		dcr,gr2
	sethi.p		%hi(DCR_SE),gr3
	setlo		%lo(DCR_SE),gr3
	andcc		gr2,gr3,gr0,icc0
	beq		icc0,#0,__break_no_user_sstep	/* must have been a BREAK insn */

	not		gr3,gr3
	and		gr2,gr3,gr2
	movgs		gr2,dcr
	ori		gr23,#REG__STATUS_STEP,gr23

__break_no_user_sstep:
	LEDS		0x300f,gr2

	movsg		brr,gr2
	andi		gr2,#BRR_ST|BRR_SB,gr2
	slli		gr2,#1,gr2
	or		gr23,gr2,gr23
	sti.p		gr23,@(gr28,#REG__STATUS)	/* record single step status */

	# adjust the value acquired from TBR - this indicates the exception
	setlos		#~TBR_TT,gr2
	and.p		gr20,gr2,gr20
	setlos		#TBR_TT_BREAK,gr2
	or.p		gr20,gr2,gr20

	# fudge PSR.PS and BPSR.BS to return to kernel mode through the trap
	# table as trap 126
	andi		gr22,#~PSR_PS,gr22		/* PSR.PS should be 0 */
	movgs		gr22,psr

	setlos		#BPSR_BS,gr2			/* BPSR.BS should be 1 and BPSR.BET 0 */
	movgs		gr2,bpsr

	# return through remainder of the exception prologue
	# - need to load gr23 with return handler address
	sethi.p		%hi(__entry_return_from_user_exception),gr23
	setlo		%lo(__entry_return_from_user_exception),gr23
	sethi.p		%hi(__entry_common),gr3
	setlo		%lo(__entry_common),gr3
	movgs		gr3,bpcsr

	LEDS		0x301f,gr2

	ldi		@(gr31,#REG_CCR),gr3
	movgs		gr3,ccr
	lddi.p		@(gr31,#REG_GR(2)),gr2
	xor		gr31,gr31,gr31
	movgs		gr0,brr
#ifdef CONFIG_MMU
	movsg		scr3,gr31
#endif
	rett		#1

###############################################################################
#
# resume normal debug-mode entry
#
###############################################################################
__break_continue:
	LEDS		0x4003,gr2

	# set up the kernel stack pointer
	sti		sp,@(gr31,#REG_SP)

	sethi.p		%hi(__break_frame_0),sp
	setlo		%lo(__break_frame_0),sp

	# finish building the exception frame
	stdi		gr4 ,@(gr31,#REG_GR(4))
	stdi		gr6 ,@(gr31,#REG_GR(6))
	stdi		gr8 ,@(gr31,#REG_GR(8))
	stdi		gr10,@(gr31,#REG_GR(10))
	stdi		gr12,@(gr31,#REG_GR(12))
	stdi		gr14,@(gr31,#REG_GR(14))
	stdi		gr16,@(gr31,#REG_GR(16))
	stdi		gr18,@(gr31,#REG_GR(18))
	stdi		gr20,@(gr31,#REG_GR(20))
	stdi		gr22,@(gr31,#REG_GR(22))
	stdi		gr24,@(gr31,#REG_GR(24))
	stdi		gr26,@(gr31,#REG_GR(26))
	sti		gr0 ,@(gr31,#REG_GR(28))	/* NULL frame pointer */
	sti		gr29,@(gr31,#REG_GR(29))
	sti		gr30,@(gr31,#REG_GR(30))
	sti		gr8 ,@(gr31,#REG_ORIG_GR8)

#ifdef CONFIG_MMU
	movsg		scr3,gr19
	sti		gr19,@(gr31,#REG_GR(31))
#endif

	movsg		bpsr ,gr19
	movsg		tbr  ,gr20
	movsg		bpcsr,gr21
	movsg		psr  ,gr22
	movsg		isr  ,gr23
	movsg		cccr ,gr25
	movsg		lr   ,gr26
	movsg		lcr  ,gr27

	andi.p		gr22,#~(PSR_S|PSR_ET),gr5	/* rebuild PSR */
	andi		gr19,#PSR_ET,gr4
	or.p		gr4,gr5,gr5
	srli		gr19,#10,gr4
	andi		gr4,#PSR_S,gr4
	or.p		gr4,gr5,gr5

	setlos		#-1,gr6
	sti		gr20,@(gr31,#REG_TBR)
	sti		gr21,@(gr31,#REG_PC)
	sti		gr5 ,@(gr31,#REG_PSR)
	sti		gr23,@(gr31,#REG_ISR)
	sti		gr25,@(gr31,#REG_CCCR)
	stdi		gr26,@(gr31,#REG_LR)
	sti		gr6 ,@(gr31,#REG_SYSCALLNO)

	# store CPU-specific regs
	movsg		iacc0h,gr4
	movsg		iacc0l,gr5
	stdi		gr4,@(gr31,#REG_IACC0)

	movsg		gner0,gr4
	movsg		gner1,gr5
	stdi		gr4,@(gr31,#REG_GNER0)

	# build the debug register frame
	movsg		brr,gr4
	movgs		gr0,brr
	movsg		nmar,gr5
	movsg		dcr,gr6

	sethi.p		%hi(__debug_status),gr7
	setlo		%lo(__debug_status),gr7

	stdi		gr4 ,@(gr7,#DEBUG_BRR)
	sti		gr19,@(gr7,#DEBUG_BPSR)
	sti.p		gr6 ,@(gr7,#DEBUG_DCR)

	# trap exceptions during break handling and disable h/w breakpoints/watchpoints
	sethi		%hi(DCR_EBE),gr5
	setlo.p		%lo(DCR_EBE),gr5
	sethi		%hi(__entry_breaktrap_table),gr4
	setlo		%lo(__entry_breaktrap_table),gr4
	movgs		gr5,dcr
	movgs		gr4,tbr

	# set up kernel global registers
	sethi.p		%hi(__kernel_current_task),gr5
	setlo		%lo(__kernel_current_task),gr5
	ld		@(gr5,gr0),gr29
	ldi.p		@(gr29,#4),gr15		; __current_thread_info = current->thread_info

	sethi		%hi(_gp),gr16
	setlo.p		%lo(_gp),gr16

	# make sure we (the kernel) get div-zero and misalignment exceptions
	setlos		#ISR_EDE|ISR_DTT_DIVBYZERO|ISR_EMAM_EXCEPTION,gr5
	movgs		gr5,isr

	# enter the GDB stub
	LEDS		0x4007,gr2

	or.p		gr0,gr0,fp
	call		debug_stub

	LEDS		0x403f,gr2

	# return from break
	lddi		@(gr31,#REG_IACC0),gr4
	movgs		gr4,iacc0h
	movgs		gr5,iacc0l

	lddi		@(gr31,#REG_GNER0),gr4
	movgs		gr4,gner0
	movgs		gr5,gner1

	lddi		@(gr31,#REG_LR)  ,gr26
	lddi		@(gr31,#REG_CCR) ,gr24
	lddi		@(gr31,#REG_PSR) ,gr22
	ldi		@(gr31,#REG_PC)  ,gr21
	ldi		@(gr31,#REG_TBR) ,gr20

	sethi.p		%hi(__debug_status),gr6
	setlo		%lo(__debug_status),gr6
	ldi.p		@(gr6,#DEBUG_DCR) ,gr6

	andi		gr22,#PSR_S,gr19		/* rebuild BPSR */
	andi.p		gr22,#PSR_ET,gr5
	slli		gr19,#10,gr19
	or		gr5,gr19,gr19

	movgs		gr6 ,dcr
	movgs		gr19,bpsr
	movgs		gr20,tbr
	movgs		gr21,bpcsr
	movgs		gr23,isr
	movgs		gr24,ccr
	movgs		gr25,cccr
	movgs		gr26,lr
	movgs		gr27,lcr

	LEDS		0x407f,gr2

#ifdef CONFIG_MMU
	ldi		@(gr31,#REG_GR(31)),gr2
	movgs		gr2,scr3
#endif

	ldi		@(gr31,#REG_GR(30)),gr30
	ldi		@(gr31,#REG_GR(29)),gr29
	lddi		@(gr31,#REG_GR(26)),gr26
	lddi		@(gr31,#REG_GR(24)),gr24
	lddi		@(gr31,#REG_GR(22)),gr22
	lddi		@(gr31,#REG_GR(20)),gr20
	lddi		@(gr31,#REG_GR(18)),gr18
	lddi		@(gr31,#REG_GR(16)),gr16
	lddi		@(gr31,#REG_GR(14)),gr14
	lddi		@(gr31,#REG_GR(12)),gr12
	lddi		@(gr31,#REG_GR(10)),gr10
	lddi		@(gr31,#REG_GR(8)) ,gr8
	lddi		@(gr31,#REG_GR(6)) ,gr6
	lddi		@(gr31,#REG_GR(4)) ,gr4
	lddi		@(gr31,#REG_GR(2)) ,gr2
	ldi.p		@(gr31,#REG_SP)    ,sp

	xor		gr31,gr31,gr31
	movgs		gr0,brr
#ifdef CONFIG_MMU
	movsg		scr3,gr31
#endif
	rett		#1

###################################################################################################
#
# GDB stub "system calls"
#
###################################################################################################

#ifdef CONFIG_GDBSTUB
	# void gdbstub_console_write(struct console *con, const char *p, unsigned n)
	.globl		gdbstub_console_write
gdbstub_console_write:
	break
	bralr
#endif

	# GDB stub BUG() trap
	# GR8 is the proposed signal number
	.globl		__debug_bug_trap
__debug_bug_trap:
	break
	bralr

	# transfer kernel exeception to GDB for handling
	.globl		__break_hijack_kernel_event
__break_hijack_kernel_event:
	break
	.globl		__break_hijack_kernel_event_breaks_here
__break_hijack_kernel_event_breaks_here:
	nop

#ifdef CONFIG_MMU
	# handle a return from TLB-miss that requires single-step reactivation
	.globl		__break_tlb_miss_return_break
__break_tlb_miss_return_break:
	break
__break_tlb_miss_return_breaks_here:
	nop
#endif

	# guard the first .text label in the next file from confusion
	nop
