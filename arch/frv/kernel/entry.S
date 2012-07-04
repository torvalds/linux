/* entry.S: FR-V entry
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 *
 * Entry to the kernel is "interesting":
 *  (1) There are no stack pointers, not even for the kernel
 *  (2) General Registers should not be clobbered
 *  (3) There are no kernel-only data registers
 *  (4) Since all addressing modes are wrt to a General Register, no global
 *      variables can be reached
 *
 * We deal with this by declaring that we shall kill GR28 on entering the
 * kernel from userspace
 *
 * However, since break interrupts can interrupt the CPU even when PSR.ET==0,
 * they can't rely on GR28 to be anything useful, and so need to clobber a
 * separate register (GR31). Break interrupts are managed in break.S
 *
 * GR29 _is_ saved, and holds the current task pointer globally
 *
 */

#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/ptrace.h>
#include <asm/errno.h>
#include <asm/cache.h>
#include <asm/spr-regs.h>

#define nr_syscalls ((syscall_table_size)/4)

	.section	.text..entry
	.balign		4

.macro LEDS val
#	sethi.p		%hi(0xe1200004),gr30
#	setlo		%lo(0xe1200004),gr30
#	setlos		#~\val,gr31
#	st		gr31,@(gr30,gr0)
#	sethi.p		%hi(0xffc00100),gr30
#	setlo		%lo(0xffc00100),gr30
#	sth		gr0,@(gr30,gr0)
#	membar
.endm

.macro LEDS32
#	not		gr31,gr31
#	sethi.p		%hi(0xe1200004),gr30
#	setlo		%lo(0xe1200004),gr30
#	st.p		gr31,@(gr30,gr0)
#	srli		gr31,#16,gr31
#	sethi.p		%hi(0xffc00100),gr30
#	setlo		%lo(0xffc00100),gr30
#	sth		gr31,@(gr30,gr0)
#	membar
.endm

###############################################################################
#
# entry point for External interrupts received whilst executing userspace code
#
###############################################################################
	.globl		__entry_uspace_external_interrupt
        .type		__entry_uspace_external_interrupt,@function
__entry_uspace_external_interrupt:
	LEDS		0x6200
	sethi.p		%hi(__kernel_frame0_ptr),gr28
	setlo		%lo(__kernel_frame0_ptr),gr28
	ldi		@(gr28,#0),gr28

	# handle h/w single-step through exceptions
	sti		gr0,@(gr28,#REG__STATUS)

	.globl		__entry_uspace_external_interrupt_reentry
__entry_uspace_external_interrupt_reentry:
	LEDS		0x6201

	setlos		#REG__END,gr30
	dcpl		gr28,gr30,#0

	# finish building the exception frame
	sti		sp,  @(gr28,#REG_SP)
	stdi		gr2, @(gr28,#REG_GR(2))
	stdi		gr4, @(gr28,#REG_GR(4))
	stdi		gr6, @(gr28,#REG_GR(6))
	stdi		gr8, @(gr28,#REG_GR(8))
	stdi		gr10,@(gr28,#REG_GR(10))
	stdi		gr12,@(gr28,#REG_GR(12))
	stdi		gr14,@(gr28,#REG_GR(14))
	stdi		gr16,@(gr28,#REG_GR(16))
	stdi		gr18,@(gr28,#REG_GR(18))
	stdi		gr20,@(gr28,#REG_GR(20))
	stdi		gr22,@(gr28,#REG_GR(22))
	stdi		gr24,@(gr28,#REG_GR(24))
	stdi		gr26,@(gr28,#REG_GR(26))
	sti		gr0, @(gr28,#REG_GR(28))
	sti		gr29,@(gr28,#REG_GR(29))
	stdi.p		gr30,@(gr28,#REG_GR(30))

	# set up the kernel stack pointer
	ori		gr28,0,sp

	movsg		tbr ,gr20
	movsg		psr ,gr22
	movsg		pcsr,gr21
	movsg		isr ,gr23
	movsg		ccr ,gr24
	movsg		cccr,gr25
	movsg		lr  ,gr26
	movsg		lcr ,gr27

	setlos.p	#-1,gr4
	andi		gr22,#PSR_PS,gr5		/* try to rebuild original PSR value */
	andi.p		gr22,#~(PSR_PS|PSR_S),gr6
	slli		gr5,#1,gr5
	or		gr6,gr5,gr5
	andi		gr5,#~PSR_ET,gr5

	sti		gr20,@(gr28,#REG_TBR)
	sti		gr21,@(gr28,#REG_PC)
	sti		gr5 ,@(gr28,#REG_PSR)
	sti		gr23,@(gr28,#REG_ISR)
	stdi		gr24,@(gr28,#REG_CCR)
	stdi		gr26,@(gr28,#REG_LR)
	sti		gr4 ,@(gr28,#REG_SYSCALLNO)

	movsg		iacc0h,gr4
	movsg		iacc0l,gr5
	stdi		gr4,@(gr28,#REG_IACC0)

	movsg		gner0,gr4
	movsg		gner1,gr5
	stdi.p		gr4,@(gr28,#REG_GNER0)

	# interrupts start off fully disabled in the interrupt handler
	subcc		gr0,gr0,gr0,icc2		/* set Z and clear C */

	# set up kernel global registers
	sethi.p		%hi(__kernel_current_task),gr5
	setlo		%lo(__kernel_current_task),gr5
	sethi.p		%hi(_gp),gr16
	setlo		%lo(_gp),gr16
	ldi		@(gr5,#0),gr29
	ldi.p		@(gr29,#4),gr15		; __current_thread_info = current->thread_info

	# make sure we (the kernel) get div-zero and misalignment exceptions
	setlos		#ISR_EDE|ISR_DTT_DIVBYZERO|ISR_EMAM_EXCEPTION,gr5
	movgs		gr5,isr

	# switch to the kernel trap table
	sethi.p		%hi(__entry_kerneltrap_table),gr6
	setlo		%lo(__entry_kerneltrap_table),gr6
	movgs		gr6,tbr

	# set the return address
	sethi.p		%hi(__entry_return_from_user_interrupt),gr4
	setlo		%lo(__entry_return_from_user_interrupt),gr4
	movgs		gr4,lr

	# raise the minimum interrupt priority to 15 (NMI only) and enable exceptions
	movsg		psr,gr4

	ori		gr4,#PSR_PIL_14,gr4
	movgs		gr4,psr
	ori		gr4,#PSR_PIL_14|PSR_ET,gr4
	movgs		gr4,psr

	LEDS		0x6202
	bra		do_IRQ

	.size		__entry_uspace_external_interrupt,.-__entry_uspace_external_interrupt

###############################################################################
#
# entry point for External interrupts received whilst executing kernel code
# - on arriving here, the following registers should already be set up:
#	GR15	- current thread_info struct pointer
#	GR16	- kernel GP-REL pointer
#	GR29	- current task struct pointer
#	TBR	- kernel trap vector table
#	ISR	- kernel's preferred integer controls
#
###############################################################################
	.globl		__entry_kernel_external_interrupt
        .type		__entry_kernel_external_interrupt,@function
__entry_kernel_external_interrupt:
	LEDS		0x6210
//	sub		sp,gr15,gr31
//	LEDS32

	# set up the stack pointer
	or.p		sp,gr0,gr30
	subi		sp,#REG__END,sp
	sti		gr30,@(sp,#REG_SP)

	# handle h/w single-step through exceptions
	sti		gr0,@(sp,#REG__STATUS)

	.globl		__entry_kernel_external_interrupt_reentry
__entry_kernel_external_interrupt_reentry:
	LEDS		0x6211

	# set up the exception frame
	setlos		#REG__END,gr30
	dcpl		sp,gr30,#0

	sti.p		gr28,@(sp,#REG_GR(28))
	ori		sp,0,gr28

	# finish building the exception frame
	stdi		gr2,@(gr28,#REG_GR(2))
	stdi		gr4,@(gr28,#REG_GR(4))
	stdi		gr6,@(gr28,#REG_GR(6))
	stdi		gr8,@(gr28,#REG_GR(8))
	stdi		gr10,@(gr28,#REG_GR(10))
	stdi		gr12,@(gr28,#REG_GR(12))
	stdi		gr14,@(gr28,#REG_GR(14))
	stdi		gr16,@(gr28,#REG_GR(16))
	stdi		gr18,@(gr28,#REG_GR(18))
	stdi		gr20,@(gr28,#REG_GR(20))
	stdi		gr22,@(gr28,#REG_GR(22))
	stdi		gr24,@(gr28,#REG_GR(24))
	stdi		gr26,@(gr28,#REG_GR(26))
	sti		gr29,@(gr28,#REG_GR(29))
	stdi.p		gr30,@(gr28,#REG_GR(30))

	# note virtual interrupts will be fully enabled upon return
	subicc		gr0,#1,gr0,icc2			/* clear Z, set C */

	movsg		tbr ,gr20
	movsg		psr ,gr22
	movsg		pcsr,gr21
	movsg		isr ,gr23
	movsg		ccr ,gr24
	movsg		cccr,gr25
	movsg		lr  ,gr26
	movsg		lcr ,gr27

	setlos.p	#-1,gr4
	andi		gr22,#PSR_PS,gr5		/* try to rebuild original PSR value */
	andi.p		gr22,#~(PSR_PS|PSR_S),gr6
	slli		gr5,#1,gr5
	or		gr6,gr5,gr5
	andi.p		gr5,#~PSR_ET,gr5

	# set CCCR.CC3 to Undefined to abort atomic-modify completion inside the kernel
	# - for an explanation of how it works, see: Documentation/frv/atomic-ops.txt
	andi		gr25,#~0xc0,gr25

	sti		gr20,@(gr28,#REG_TBR)
	sti		gr21,@(gr28,#REG_PC)
	sti		gr5 ,@(gr28,#REG_PSR)
	sti		gr23,@(gr28,#REG_ISR)
	stdi		gr24,@(gr28,#REG_CCR)
	stdi		gr26,@(gr28,#REG_LR)
	sti		gr4 ,@(gr28,#REG_SYSCALLNO)

	movsg		iacc0h,gr4
	movsg		iacc0l,gr5
	stdi		gr4,@(gr28,#REG_IACC0)

	movsg		gner0,gr4
	movsg		gner1,gr5
	stdi.p		gr4,@(gr28,#REG_GNER0)

	# interrupts start off fully disabled in the interrupt handler
	subcc		gr0,gr0,gr0,icc2			/* set Z and clear C */

	# set the return address
	sethi.p		%hi(__entry_return_from_kernel_interrupt),gr4
	setlo		%lo(__entry_return_from_kernel_interrupt),gr4
	movgs		gr4,lr

	# clear power-saving mode flags
	movsg		hsr0,gr4
	andi		gr4,#~HSR0_PDM,gr4
	movgs		gr4,hsr0

	# raise the minimum interrupt priority to 15 (NMI only) and enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_PIL_14,gr4
	movgs		gr4,psr
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr

	LEDS		0x6212
	bra		do_IRQ

	.size		__entry_kernel_external_interrupt,.-__entry_kernel_external_interrupt

###############################################################################
#
# deal with interrupts that were actually virtually disabled
# - we need to really disable them, flag the fact and return immediately
# - if you change this, you must alter break.S also
#
###############################################################################
	.balign		L1_CACHE_BYTES
	.globl		__entry_kernel_external_interrupt_virtually_disabled
	.type		__entry_kernel_external_interrupt_virtually_disabled,@function
__entry_kernel_external_interrupt_virtually_disabled:
	movsg		psr,gr30
	andi		gr30,#~PSR_PIL,gr30
	ori		gr30,#PSR_PIL_14,gr30		; debugging interrupts only
	movgs		gr30,psr
	subcc		gr0,gr0,gr0,icc2		; leave Z set, clear C
	rett		#0

	.size		__entry_kernel_external_interrupt_virtually_disabled,.-__entry_kernel_external_interrupt_virtually_disabled

###############################################################################
#
# deal with re-enablement of interrupts that were pending when virtually re-enabled
# - set ICC2.C, re-enable the real interrupts and return
# - we can clear ICC2.Z because we shouldn't be here if it's not 0 [due to TIHI]
# - if you change this, you must alter break.S also
#
###############################################################################
	.balign		L1_CACHE_BYTES
	.globl		__entry_kernel_external_interrupt_virtual_reenable
	.type		__entry_kernel_external_interrupt_virtual_reenable,@function
__entry_kernel_external_interrupt_virtual_reenable:
	movsg		psr,gr30
	andi		gr30,#~PSR_PIL,gr30		; re-enable interrupts
	movgs		gr30,psr
	subicc		gr0,#1,gr0,icc2			; clear Z, set C
	rett		#0

	.size		__entry_kernel_external_interrupt_virtual_reenable,.-__entry_kernel_external_interrupt_virtual_reenable

###############################################################################
#
# entry point for Software and Progam interrupts generated whilst executing userspace code
#
###############################################################################
	.globl		__entry_uspace_softprog_interrupt
        .type		__entry_uspace_softprog_interrupt,@function
	.globl		__entry_uspace_handle_mmu_fault
__entry_uspace_softprog_interrupt:
	LEDS		0x6000
#ifdef CONFIG_MMU
	movsg		ear0,gr28
__entry_uspace_handle_mmu_fault:
	movgs		gr28,scr2
#endif
	sethi.p		%hi(__kernel_frame0_ptr),gr28
	setlo		%lo(__kernel_frame0_ptr),gr28
	ldi		@(gr28,#0),gr28

	# handle h/w single-step through exceptions
	sti		gr0,@(gr28,#REG__STATUS)

	.globl		__entry_uspace_softprog_interrupt_reentry
__entry_uspace_softprog_interrupt_reentry:
	LEDS		0x6001

	setlos		#REG__END,gr30
	dcpl		gr28,gr30,#0

	# set up the kernel stack pointer
	sti.p		sp,@(gr28,#REG_SP)
	ori		gr28,0,sp
	sti		gr0,@(gr28,#REG_GR(28))

	stdi		gr20,@(gr28,#REG_GR(20))
	stdi		gr22,@(gr28,#REG_GR(22))

	movsg		tbr,gr20
	movsg		pcsr,gr21
	movsg		psr,gr22

	sethi.p		%hi(__entry_return_from_user_exception),gr23
	setlo		%lo(__entry_return_from_user_exception),gr23

	bra		__entry_common

	.size		__entry_uspace_softprog_interrupt,.-__entry_uspace_softprog_interrupt

	# single-stepping was disabled on entry to a TLB handler that then faulted
#ifdef CONFIG_MMU
	.globl		__entry_uspace_handle_mmu_fault_sstep
__entry_uspace_handle_mmu_fault_sstep:
	movgs		gr28,scr2
	sethi.p		%hi(__kernel_frame0_ptr),gr28
	setlo		%lo(__kernel_frame0_ptr),gr28
	ldi		@(gr28,#0),gr28

	# flag single-step re-enablement
	sti		gr0,@(gr28,#REG__STATUS)
	bra		__entry_uspace_softprog_interrupt_reentry
#endif


###############################################################################
#
# entry point for Software and Progam interrupts generated whilst executing kernel code
#
###############################################################################
	.globl		__entry_kernel_softprog_interrupt
        .type		__entry_kernel_softprog_interrupt,@function
__entry_kernel_softprog_interrupt:
	LEDS		0x6004

#ifdef CONFIG_MMU
	movsg		ear0,gr30
	movgs		gr30,scr2
#endif

	.globl		__entry_kernel_handle_mmu_fault
__entry_kernel_handle_mmu_fault:
	# set up the stack pointer
	subi		sp,#REG__END,sp
	sti		sp,@(sp,#REG_SP)
	sti		sp,@(sp,#REG_SP-4)
	andi		sp,#~7,sp

	# handle h/w single-step through exceptions
	sti		gr0,@(sp,#REG__STATUS)

	.globl		__entry_kernel_softprog_interrupt_reentry
__entry_kernel_softprog_interrupt_reentry:
	LEDS		0x6005

	setlos		#REG__END,gr30
	dcpl		sp,gr30,#0

	# set up the exception frame
	sti.p		gr28,@(sp,#REG_GR(28))
	ori		sp,0,gr28

	stdi		gr20,@(gr28,#REG_GR(20))
	stdi		gr22,@(gr28,#REG_GR(22))

	ldi		@(sp,#REG_SP),gr22		/* reconstruct the old SP */
	addi		gr22,#REG__END,gr22
	sti		gr22,@(sp,#REG_SP)

	# set CCCR.CC3 to Undefined to abort atomic-modify completion inside the kernel
	# - for an explanation of how it works, see: Documentation/frv/atomic-ops.txt
	movsg		cccr,gr20
	andi		gr20,#~0xc0,gr20
	movgs		gr20,cccr

	movsg		tbr,gr20
	movsg		pcsr,gr21
	movsg		psr,gr22

	sethi.p		%hi(__entry_return_from_kernel_exception),gr23
	setlo		%lo(__entry_return_from_kernel_exception),gr23
	bra		__entry_common

	.size		__entry_kernel_softprog_interrupt,.-__entry_kernel_softprog_interrupt

	# single-stepping was disabled on entry to a TLB handler that then faulted
#ifdef CONFIG_MMU
	.globl		__entry_kernel_handle_mmu_fault_sstep
__entry_kernel_handle_mmu_fault_sstep:
	# set up the stack pointer
	subi		sp,#REG__END,sp
	sti		sp,@(sp,#REG_SP)
	sti		sp,@(sp,#REG_SP-4)
	andi		sp,#~7,sp

	# flag single-step re-enablement
	sethi		#REG__STATUS_STEP,gr30
	sti		gr30,@(sp,#REG__STATUS)
	bra		__entry_kernel_softprog_interrupt_reentry
#endif


###############################################################################
#
# the rest of the kernel entry point code
# - on arriving here, the following registers should be set up:
#	GR1	- kernel stack pointer
#	GR7	- syscall number (trap 0 only)
#	GR8-13	- syscall args (trap 0 only)
#	GR20	- saved TBR
#	GR21	- saved PC
#	GR22	- saved PSR
#	GR23	- return handler address
#	GR28	- exception frame on stack
#	SCR2	- saved EAR0 where applicable (clobbered by ICI & ICEF insns on FR451)
#	PSR	- PSR.S 1, PSR.ET 0
#
###############################################################################
	.globl		__entry_common
        .type		__entry_common,@function
__entry_common:
	LEDS		0x6008

	# finish building the exception frame
	stdi		gr2,@(gr28,#REG_GR(2))
	stdi		gr4,@(gr28,#REG_GR(4))
	stdi		gr6,@(gr28,#REG_GR(6))
	stdi		gr8,@(gr28,#REG_GR(8))
	stdi		gr10,@(gr28,#REG_GR(10))
	stdi		gr12,@(gr28,#REG_GR(12))
	stdi		gr14,@(gr28,#REG_GR(14))
	stdi		gr16,@(gr28,#REG_GR(16))
	stdi		gr18,@(gr28,#REG_GR(18))
	stdi		gr24,@(gr28,#REG_GR(24))
	stdi		gr26,@(gr28,#REG_GR(26))
	sti		gr29,@(gr28,#REG_GR(29))
	stdi		gr30,@(gr28,#REG_GR(30))

	movsg		lcr ,gr27
	movsg		lr  ,gr26
	movgs		gr23,lr
	movsg		cccr,gr25
	movsg		ccr ,gr24
	movsg		isr ,gr23

	setlos.p	#-1,gr4
	andi		gr22,#PSR_PS,gr5		/* try to rebuild original PSR value */
	andi.p		gr22,#~(PSR_PS|PSR_S),gr6
	slli		gr5,#1,gr5
	or		gr6,gr5,gr5
	andi		gr5,#~PSR_ET,gr5

	sti		gr20,@(gr28,#REG_TBR)
	sti		gr21,@(gr28,#REG_PC)
	sti		gr5 ,@(gr28,#REG_PSR)
	sti		gr23,@(gr28,#REG_ISR)
	stdi		gr24,@(gr28,#REG_CCR)
	stdi		gr26,@(gr28,#REG_LR)
	sti		gr4 ,@(gr28,#REG_SYSCALLNO)

	movsg		iacc0h,gr4
	movsg		iacc0l,gr5
	stdi		gr4,@(gr28,#REG_IACC0)

	movsg		gner0,gr4
	movsg		gner1,gr5
	stdi.p		gr4,@(gr28,#REG_GNER0)

	# set up virtual interrupt disablement
	subicc		gr0,#1,gr0,icc2			/* clear Z flag, set C flag */

	# set up kernel global registers
	sethi.p		%hi(__kernel_current_task),gr5
	setlo		%lo(__kernel_current_task),gr5
	sethi.p		%hi(_gp),gr16
	setlo		%lo(_gp),gr16
	ldi		@(gr5,#0),gr29
	ldi		@(gr29,#4),gr15		; __current_thread_info = current->thread_info

	# switch to the kernel trap table
	sethi.p		%hi(__entry_kerneltrap_table),gr6
	setlo		%lo(__entry_kerneltrap_table),gr6
	movgs		gr6,tbr

	# make sure we (the kernel) get div-zero and misalignment exceptions
	setlos		#ISR_EDE|ISR_DTT_DIVBYZERO|ISR_EMAM_EXCEPTION,gr5
	movgs		gr5,isr

	# clear power-saving mode flags
	movsg		hsr0,gr4
	andi		gr4,#~HSR0_PDM,gr4
	movgs		gr4,hsr0

	# multiplex again using old TBR as a guide
	setlos.p	#TBR_TT,gr3
	sethi		%hi(__entry_vector_table),gr6
	and.p		gr20,gr3,gr5
	setlo		%lo(__entry_vector_table),gr6
	srli		gr5,#2,gr5
	ld		@(gr5,gr6),gr5

	LEDS		0x6009
	jmpl		@(gr5,gr0)


	.size		__entry_common,.-__entry_common

###############################################################################
#
# handle instruction MMU fault
#
###############################################################################
#ifdef CONFIG_MMU
	.globl		__entry_insn_mmu_fault
__entry_insn_mmu_fault:
	LEDS		0x6010
	setlos		#0,gr8
	movsg		esr0,gr9
	movsg		scr2,gr10

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr

	sethi.p		%hi(do_page_fault),gr5
	setlo		%lo(do_page_fault),gr5
	jmpl		@(gr5,gr0)	; call do_page_fault(0,esr0,ear0)
#endif


###############################################################################
#
# handle instruction access error
#
###############################################################################
	.globl		__entry_insn_access_error
__entry_insn_access_error:
	LEDS		0x6011
	sethi.p		%hi(insn_access_error),gr5
	setlo		%lo(insn_access_error),gr5
	movsg		esfr1,gr8
	movsg		epcr0,gr9
	movsg		esr0,gr10

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call insn_access_error(esfr1,epcr0,esr0)

###############################################################################
#
# handle various instructions of dubious legality
#
###############################################################################
	.globl		__entry_unsupported_trap
	.globl		__entry_illegal_instruction
	.globl		__entry_privileged_instruction
	.globl		__entry_debug_exception
__entry_unsupported_trap:
	subi		gr21,#4,gr21
	sti		gr21,@(gr28,#REG_PC)
__entry_illegal_instruction:
__entry_privileged_instruction:
__entry_debug_exception:
	LEDS		0x6012
	sethi.p		%hi(illegal_instruction),gr5
	setlo		%lo(illegal_instruction),gr5
	movsg		esfr1,gr8
	movsg		epcr0,gr9
	movsg		esr0,gr10

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call ill_insn(esfr1,epcr0,esr0)

###############################################################################
#
# handle atomic operation emulation for userspace
#
###############################################################################
	.globl		__entry_atomic_op
__entry_atomic_op:
	LEDS		0x6012
	sethi.p		%hi(atomic_operation),gr5
	setlo		%lo(atomic_operation),gr5
	movsg		esfr1,gr8
	movsg		epcr0,gr9
	movsg		esr0,gr10

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call atomic_operation(esfr1,epcr0,esr0)

###############################################################################
#
# handle media exception
#
###############################################################################
	.globl		__entry_media_exception
__entry_media_exception:
	LEDS		0x6013
	sethi.p		%hi(media_exception),gr5
	setlo		%lo(media_exception),gr5
	movsg		msr0,gr8
	movsg		msr1,gr9

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call media_excep(msr0,msr1)

###############################################################################
#
# handle data MMU fault
# handle data DAT fault (write-protect exception)
#
###############################################################################
#ifdef CONFIG_MMU
	.globl		__entry_data_mmu_fault
__entry_data_mmu_fault:
	.globl		__entry_data_dat_fault
__entry_data_dat_fault:
	LEDS		0x6014
	setlos		#1,gr8
	movsg		esr0,gr9
	movsg		scr2,gr10	; saved EAR0

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr

	sethi.p		%hi(do_page_fault),gr5
	setlo		%lo(do_page_fault),gr5
	jmpl		@(gr5,gr0)	; call do_page_fault(1,esr0,ear0)
#endif

###############################################################################
#
# handle data and instruction access exceptions
#
###############################################################################
	.globl		__entry_insn_access_exception
	.globl		__entry_data_access_exception
__entry_insn_access_exception:
__entry_data_access_exception:
	LEDS		0x6016
	sethi.p		%hi(memory_access_exception),gr5
	setlo		%lo(memory_access_exception),gr5
	movsg		esr0,gr8
	movsg		scr2,gr9	; saved EAR0
	movsg		epcr0,gr10

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call memory_access_error(esr0,ear0,epcr0)

###############################################################################
#
# handle data access error
#
###############################################################################
	.globl		__entry_data_access_error
__entry_data_access_error:
	LEDS		0x6016
	sethi.p		%hi(data_access_error),gr5
	setlo		%lo(data_access_error),gr5
	movsg		esfr1,gr8
	movsg		esr15,gr9
	movsg		ear15,gr10

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call data_access_error(esfr1,esr15,ear15)

###############################################################################
#
# handle data store error
#
###############################################################################
	.globl		__entry_data_store_error
__entry_data_store_error:
	LEDS		0x6017
	sethi.p		%hi(data_store_error),gr5
	setlo		%lo(data_store_error),gr5
	movsg		esfr1,gr8
	movsg		esr14,gr9

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call data_store_error(esfr1,esr14)

###############################################################################
#
# handle division exception
#
###############################################################################
	.globl		__entry_division_exception
__entry_division_exception:
	LEDS		0x6018
	sethi.p		%hi(division_exception),gr5
	setlo		%lo(division_exception),gr5
	movsg		esfr1,gr8
	movsg		esr0,gr9
	movsg		isr,gr10

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call div_excep(esfr1,esr0,isr)

###############################################################################
#
# handle compound exception
#
###############################################################################
	.globl		__entry_compound_exception
__entry_compound_exception:
	LEDS		0x6019
	sethi.p		%hi(compound_exception),gr5
	setlo		%lo(compound_exception),gr5
	movsg		esfr1,gr8
	movsg		esr0,gr9
	movsg		esr14,gr10
	movsg		esr15,gr11
	movsg		msr0,gr12
	movsg		msr1,gr13

	# now that we've accessed the exception regs, we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	jmpl		@(gr5,gr0)	; call comp_excep(esfr1,esr0,esr14,esr15,msr0,msr1)

###############################################################################
#
# handle interrupts and NMIs
#
###############################################################################
	.globl		__entry_do_IRQ
__entry_do_IRQ:
	LEDS		0x6020

	# we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	bra		do_IRQ

	.globl		__entry_do_NMI
__entry_do_NMI:
	LEDS		0x6021

	# we can enable exceptions
	movsg		psr,gr4
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr
	bra		do_NMI

###############################################################################
#
# the return path for a newly forked child process
# - __switch_to() saved the old current pointer in GR8 for us
#
###############################################################################
	.globl		ret_from_fork
ret_from_fork:
	LEDS		0x6100
	call		schedule_tail

	# fork & co. return 0 to child
	setlos.p	#0,gr8
	bra		__syscall_exit

###################################################################################################
#
# Return to user mode is not as complex as all this looks,
# but we want the default path for a system call return to
# go as quickly as possible which is why some of this is
# less clear than it otherwise should be.
#
###################################################################################################
	.balign		L1_CACHE_BYTES
	.globl		system_call
system_call:
	LEDS		0x6101
	movsg		psr,gr4			; enable exceptions
	ori		gr4,#PSR_ET,gr4
	movgs		gr4,psr

	sti		gr7,@(gr28,#REG_SYSCALLNO)
	sti.p		gr8,@(gr28,#REG_ORIG_GR8)

	subicc		gr7,#nr_syscalls,gr0,icc0
	bnc		icc0,#0,__syscall_badsys

	ldi		@(gr15,#TI_FLAGS),gr4
	andicc		gr4,#_TIF_SYSCALL_TRACE,gr0,icc0
	bne		icc0,#0,__syscall_trace_entry

__syscall_call:
	slli.p		gr7,#2,gr7
	sethi		%hi(sys_call_table),gr5
	setlo		%lo(sys_call_table),gr5
	ld		@(gr5,gr7),gr4
	calll		@(gr4,gr0)


###############################################################################
#
# return to interrupted process
#
###############################################################################
__syscall_exit:
	LEDS		0x6300

	# keep current PSR in GR23
	movsg		psr,gr23

	ldi		@(gr28,#REG_PSR),gr22

	sti.p		gr8,@(gr28,#REG_GR(8))	; save return value

	# rebuild saved psr - execve will change it for init/main.c
	srli		gr22,#1,gr5
	andi.p		gr22,#~PSR_PS,gr22
	andi		gr5,#PSR_PS,gr5
	or		gr5,gr22,gr22
	ori.p		gr22,#PSR_S,gr22

	# make sure we don't miss an interrupt setting need_resched or sigpending between
	# sampling and the RETT
	ori		gr23,#PSR_PIL_14,gr23
	movgs		gr23,psr

	ldi		@(gr15,#TI_FLAGS),gr4
	andicc		gr4,#_TIF_ALLWORK_MASK,gr0,icc0
	bne		icc0,#0,__syscall_exit_work

	# restore all registers and return
__entry_return_direct:
	LEDS		0x6301

	andi		gr22,#~PSR_ET,gr22
	movgs		gr22,psr

	ldi		@(gr28,#REG_ISR),gr23
	lddi		@(gr28,#REG_CCR),gr24
	lddi		@(gr28,#REG_LR) ,gr26
	ldi		@(gr28,#REG_PC) ,gr21
	ldi		@(gr28,#REG_TBR),gr20

	movgs		gr20,tbr
	movgs		gr21,pcsr
	movgs		gr23,isr
	movgs		gr24,ccr
	movgs		gr25,cccr
	movgs		gr26,lr
	movgs		gr27,lcr

	lddi		@(gr28,#REG_GNER0),gr4
	movgs		gr4,gner0
	movgs		gr5,gner1

	lddi		@(gr28,#REG_IACC0),gr4
	movgs		gr4,iacc0h
	movgs		gr5,iacc0l

	lddi		@(gr28,#REG_GR(4)) ,gr4
	lddi		@(gr28,#REG_GR(6)) ,gr6
	lddi		@(gr28,#REG_GR(8)) ,gr8
	lddi		@(gr28,#REG_GR(10)),gr10
	lddi		@(gr28,#REG_GR(12)),gr12
	lddi		@(gr28,#REG_GR(14)),gr14
	lddi		@(gr28,#REG_GR(16)),gr16
	lddi		@(gr28,#REG_GR(18)),gr18
	lddi		@(gr28,#REG_GR(20)),gr20
	lddi		@(gr28,#REG_GR(22)),gr22
	lddi		@(gr28,#REG_GR(24)),gr24
	lddi		@(gr28,#REG_GR(26)),gr26
	ldi		@(gr28,#REG_GR(29)),gr29
	lddi		@(gr28,#REG_GR(30)),gr30

	# check to see if a debugging return is required
	LEDS		0x67f0
	movsg		ccr,gr2
	ldi		@(gr28,#REG__STATUS),gr3
	andicc		gr3,#REG__STATUS_STEP,gr0,icc0
	bne		icc0,#0,__entry_return_singlestep
	movgs		gr2,ccr

	ldi		@(gr28,#REG_SP)    ,sp
	lddi		@(gr28,#REG_GR(2)) ,gr2
	ldi		@(gr28,#REG_GR(28)),gr28

	LEDS		0x67fe
//	movsg		pcsr,gr31
//	LEDS32

#if 0
	# store the current frame in the workram on the FR451
	movgs		gr28,scr2
	sethi.p		%hi(0xfe800000),gr28
	setlo		%lo(0xfe800000),gr28

	stdi		gr2,@(gr28,#REG_GR(2))
	stdi		gr4,@(gr28,#REG_GR(4))
	stdi		gr6,@(gr28,#REG_GR(6))
	stdi		gr8,@(gr28,#REG_GR(8))
	stdi		gr10,@(gr28,#REG_GR(10))
	stdi		gr12,@(gr28,#REG_GR(12))
	stdi		gr14,@(gr28,#REG_GR(14))
	stdi		gr16,@(gr28,#REG_GR(16))
	stdi		gr18,@(gr28,#REG_GR(18))
	stdi		gr24,@(gr28,#REG_GR(24))
	stdi		gr26,@(gr28,#REG_GR(26))
	sti		gr29,@(gr28,#REG_GR(29))
	stdi		gr30,@(gr28,#REG_GR(30))

	movsg		tbr ,gr30
	sti		gr30,@(gr28,#REG_TBR)
	movsg		pcsr,gr30
	sti		gr30,@(gr28,#REG_PC)
	movsg		psr ,gr30
	sti		gr30,@(gr28,#REG_PSR)
	movsg		isr ,gr30
	sti		gr30,@(gr28,#REG_ISR)
	movsg		ccr ,gr30
	movsg		cccr,gr31
	stdi		gr30,@(gr28,#REG_CCR)
	movsg		lr  ,gr30
	movsg		lcr ,gr31
	stdi		gr30,@(gr28,#REG_LR)
	sti		gr0 ,@(gr28,#REG_SYSCALLNO)
	movsg		scr2,gr28
#endif

	rett		#0

	# return via break.S
__entry_return_singlestep:
	movgs		gr2,ccr
	lddi		@(gr28,#REG_GR(2)) ,gr2
	ldi		@(gr28,#REG_SP)    ,sp
	ldi		@(gr28,#REG_GR(28)),gr28
	LEDS		0x67ff
	break
	.globl		__entry_return_singlestep_breaks_here
__entry_return_singlestep_breaks_here:
	nop


###############################################################################
#
# return to a process interrupted in kernel space
# - we need to consider preemption if that is enabled
#
###############################################################################
	.balign		L1_CACHE_BYTES
__entry_return_from_kernel_exception:
	LEDS		0x6302
	movsg		psr,gr23
	ori		gr23,#PSR_PIL_14,gr23
	movgs		gr23,psr
	bra		__entry_return_direct

	.balign		L1_CACHE_BYTES
__entry_return_from_kernel_interrupt:
	LEDS		0x6303
	movsg		psr,gr23
	ori		gr23,#PSR_PIL_14,gr23
	movgs		gr23,psr

#ifdef CONFIG_PREEMPT
	ldi		@(gr15,#TI_PRE_COUNT),gr5
	subicc		gr5,#0,gr0,icc0
	beq		icc0,#0,__entry_return_direct

__entry_preempt_need_resched:
	ldi		@(gr15,#TI_FLAGS),gr4
	andicc		gr4,#_TIF_NEED_RESCHED,gr0,icc0
	beq		icc0,#1,__entry_return_direct

	setlos		#PREEMPT_ACTIVE,gr5
	sti		gr5,@(gr15,#TI_FLAGS)

	andi		gr23,#~PSR_PIL,gr23
	movgs		gr23,psr

	call		schedule
	sti		gr0,@(gr15,#TI_PRE_COUNT)

	movsg		psr,gr23
	ori		gr23,#PSR_PIL_14,gr23
	movgs		gr23,psr
	bra		__entry_preempt_need_resched
#else
	bra		__entry_return_direct
#endif


###############################################################################
#
# perform work that needs to be done immediately before resumption
#
###############################################################################
	.globl		__entry_return_from_user_exception
	.balign		L1_CACHE_BYTES
__entry_return_from_user_exception:
	LEDS		0x6501

__entry_resume_userspace:
	# make sure we don't miss an interrupt setting need_resched or sigpending between
	# sampling and the RETT
	movsg		psr,gr23
	ori		gr23,#PSR_PIL_14,gr23
	movgs		gr23,psr

__entry_return_from_user_interrupt:
	LEDS		0x6402
	ldi		@(gr15,#TI_FLAGS),gr4
	andicc		gr4,#_TIF_WORK_MASK,gr0,icc0
	beq		icc0,#1,__entry_return_direct

__entry_work_pending:
	LEDS		0x6404
	andicc		gr4,#_TIF_NEED_RESCHED,gr0,icc0
	beq		icc0,#1,__entry_work_notifysig

__entry_work_resched:
	LEDS		0x6408
	movsg		psr,gr23
	andi		gr23,#~PSR_PIL,gr23
	movgs		gr23,psr
	call		schedule
	movsg		psr,gr23
	ori		gr23,#PSR_PIL_14,gr23
	movgs		gr23,psr

	LEDS		0x6401
	ldi		@(gr15,#TI_FLAGS),gr4
	andicc		gr4,#_TIF_WORK_MASK,gr0,icc0
	beq		icc0,#1,__entry_return_direct
	andicc		gr4,#_TIF_NEED_RESCHED,gr0,icc0
	bne		icc0,#1,__entry_work_resched

__entry_work_notifysig:
	LEDS		0x6410
	ori.p		gr4,#0,gr8
	call		do_notify_resume
	bra		__entry_resume_userspace

	# perform syscall entry tracing
__syscall_trace_entry:
	LEDS		0x6320
	call		syscall_trace_entry

	lddi.p		@(gr28,#REG_GR(8)) ,gr8
	ori		gr8,#0,gr7		; syscall_trace_entry() returned new syscallno
	lddi		@(gr28,#REG_GR(10)),gr10
	lddi.p		@(gr28,#REG_GR(12)),gr12

	subicc		gr7,#nr_syscalls,gr0,icc0
	bnc		icc0,#0,__syscall_badsys
	bra		__syscall_call

	# perform syscall exit tracing
__syscall_exit_work:
	LEDS		0x6340
	andicc		gr22,#PSR_PS,gr0,icc1	; don't handle on return to kernel mode
	andicc.p	gr4,#_TIF_SYSCALL_TRACE,gr0,icc0
	bne		icc1,#0,__entry_return_direct
	beq		icc0,#1,__entry_work_pending

	movsg		psr,gr23
	andi		gr23,#~PSR_PIL,gr23	; could let syscall_trace_exit() call schedule()
	movgs		gr23,psr

	call		syscall_trace_exit
	bra		__entry_resume_userspace

__syscall_badsys:
	LEDS		0x6380
	setlos		#-ENOSYS,gr8
	sti		gr8,@(gr28,#REG_GR(8))	; save return value
	bra		__entry_resume_userspace


###############################################################################
#
# syscall vector table
#
###############################################################################
	.section .rodata
ALIGN
	.globl		sys_call_table
sys_call_table:
	.long sys_restart_syscall	/* 0 - old "setup()" system call, used for restarting */
	.long sys_exit
	.long sys_fork
	.long sys_read
	.long sys_write
	.long sys_open		/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink		/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_lchown16
	.long sys_ni_syscall			/* old break syscall holder */
	.long sys_stat
	.long sys_lseek
	.long sys_getpid		/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_setuid16
	.long sys_getuid16
	.long sys_ni_syscall // sys_stime		/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_fstat
	.long sys_pause
	.long sys_utime		/* 30 */
	.long sys_ni_syscall			/* old stty syscall holder */
	.long sys_ni_syscall			/* old gtty syscall holder */
	.long sys_access
	.long sys_nice
	.long sys_ni_syscall	/* 35 */	/* old ftime syscall holder */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir		/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall			/* old prof syscall holder */
	.long sys_brk		/* 45 */
	.long sys_setgid16
	.long sys_getgid16
	.long sys_ni_syscall // sys_signal
	.long sys_geteuid16
	.long sys_getegid16	/* 50 */
	.long sys_acct
	.long sys_umount				/* recycled never used phys( */
	.long sys_ni_syscall			/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_ni_syscall			/* old mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall			/* old ulimit syscall holder */
	.long sys_ni_syscall			/* old old uname syscall */
	.long sys_umask		/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp	/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_ni_syscall // sys_sgetmask
	.long sys_ni_syscall // sys_ssetmask
	.long sys_setreuid16	/* 70 */
	.long sys_setregid16
	.long sys_sigsuspend
	.long sys_ni_syscall // sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit	/* 75 */
	.long sys_ni_syscall // sys_old_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups16	/* 80 */
	.long sys_setgroups16
	.long sys_ni_syscall			/* old_select slot */
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink		/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long sys_ni_syscall // old_readdir
	.long sys_ni_syscall	/* 90 */	/* old_mmap slot */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown16		/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall			/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs		/* 100 */
	.long sys_ni_syscall			/* ioperm for i386 */
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_ni_syscall	/* obsolete olduname( syscall */
	.long sys_ni_syscall	/* iopl for i386 */ /* 110 */
	.long sys_vhangup
	.long sys_ni_syscall	/* obsolete idle( syscall */
	.long sys_ni_syscall	/* vm86old for i386 */
	.long sys_wait4
	.long sys_swapoff		/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long sys_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_ni_syscall	/* old "cacheflush" */
	.long sys_adjtimex
	.long sys_mprotect	/* 125 */
	.long sys_sigprocmask
	.long sys_ni_syscall	/* old "create_module" */
	.long sys_init_module
	.long sys_delete_module
	.long sys_ni_syscall	/* old "get_kernel_syms" */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs		/* 135 */
	.long sys_personality
	.long sys_ni_syscall	/* for afs_syscall */
	.long sys_setfsuid16
	.long sys_setfsgid16
	.long sys_llseek		/* 140 */
	.long sys_getdents
	.long sys_select
	.long sys_flock
	.long sys_msync
	.long sys_readv		/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam   /* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min  /* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid16
	.long sys_getresuid16	/* 165 */
	.long sys_ni_syscall	/* for vm86 */
	.long sys_ni_syscall	/* Old sys_query_module */
	.long sys_poll
	.long sys_ni_syscall	/* Old nfsservctl */
	.long sys_setresgid16	/* 170 */
	.long sys_getresgid16
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask	/* 175 */
	.long sys_rt_sigpending
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread64		/* 180 */
	.long sys_pwrite64
	.long sys_chown16
	.long sys_getcwd
	.long sys_capget
	.long sys_capset           /* 185 */
	.long sys_sigaltstack
	.long sys_sendfile
	.long sys_ni_syscall		/* streams1 */
	.long sys_ni_syscall		/* streams2 */
	.long sys_vfork            /* 190 */
	.long sys_getrlimit
	.long sys_mmap2
	.long sys_truncate64
	.long sys_ftruncate64
	.long sys_stat64		/* 195 */
	.long sys_lstat64
	.long sys_fstat64
	.long sys_lchown
	.long sys_getuid
	.long sys_getgid		/* 200 */
	.long sys_geteuid
	.long sys_getegid
	.long sys_setreuid
	.long sys_setregid
	.long sys_getgroups	/* 205 */
	.long sys_setgroups
	.long sys_fchown
	.long sys_setresuid
	.long sys_getresuid
	.long sys_setresgid	/* 210 */
	.long sys_getresgid
	.long sys_chown
	.long sys_setuid
	.long sys_setgid
	.long sys_setfsuid		/* 215 */
	.long sys_setfsgid
	.long sys_pivot_root
	.long sys_mincore
	.long sys_madvise
	.long sys_getdents64	/* 220 */
	.long sys_fcntl64
	.long sys_ni_syscall	/* reserved for TUX */
	.long sys_ni_syscall	/* Reserved for Security */
	.long sys_gettid
	.long sys_readahead	/* 225 */
	.long sys_setxattr
	.long sys_lsetxattr
	.long sys_fsetxattr
	.long sys_getxattr
	.long sys_lgetxattr	/* 230 */
	.long sys_fgetxattr
	.long sys_listxattr
	.long sys_llistxattr
	.long sys_flistxattr
	.long sys_removexattr	/* 235 */
	.long sys_lremovexattr
	.long sys_fremovexattr
 	.long sys_tkill
	.long sys_sendfile64
	.long sys_futex		/* 240 */
	.long sys_sched_setaffinity
	.long sys_sched_getaffinity
	.long sys_ni_syscall	//sys_set_thread_area
	.long sys_ni_syscall	//sys_get_thread_area
	.long sys_io_setup	/* 245 */
	.long sys_io_destroy
	.long sys_io_getevents
	.long sys_io_submit
	.long sys_io_cancel
	.long sys_fadvise64	/* 250 */
	.long sys_ni_syscall
	.long sys_exit_group
	.long sys_lookup_dcookie
	.long sys_epoll_create
	.long sys_epoll_ctl	/* 255 */
	.long sys_epoll_wait
 	.long sys_remap_file_pages
 	.long sys_set_tid_address
 	.long sys_timer_create
 	.long sys_timer_settime		/* 260 */
 	.long sys_timer_gettime
 	.long sys_timer_getoverrun
 	.long sys_timer_delete
 	.long sys_clock_settime
 	.long sys_clock_gettime		/* 265 */
 	.long sys_clock_getres
 	.long sys_clock_nanosleep
	.long sys_statfs64
	.long sys_fstatfs64
	.long sys_tgkill	/* 270 */
	.long sys_utimes
 	.long sys_fadvise64_64
	.long sys_ni_syscall	/* sys_vserver */
	.long sys_mbind
	.long sys_get_mempolicy
	.long sys_set_mempolicy
	.long sys_mq_open
	.long sys_mq_unlink
	.long sys_mq_timedsend
	.long sys_mq_timedreceive	/* 280 */
	.long sys_mq_notify
	.long sys_mq_getsetattr
	.long sys_ni_syscall		/* reserved for kexec */
	.long sys_waitid
	.long sys_ni_syscall		/* 285 */ /* available */
	.long sys_add_key
	.long sys_request_key
	.long sys_keyctl
	.long sys_ioprio_set
	.long sys_ioprio_get		/* 290 */
	.long sys_inotify_init
	.long sys_inotify_add_watch
	.long sys_inotify_rm_watch
	.long sys_migrate_pages
	.long sys_openat		/* 295 */
	.long sys_mkdirat
	.long sys_mknodat
	.long sys_fchownat
	.long sys_futimesat
	.long sys_fstatat64		/* 300 */
	.long sys_unlinkat
	.long sys_renameat
	.long sys_linkat
	.long sys_symlinkat
	.long sys_readlinkat		/* 305 */
	.long sys_fchmodat
	.long sys_faccessat
	.long sys_pselect6
	.long sys_ppoll
	.long sys_unshare		/* 310 */
	.long sys_set_robust_list
	.long sys_get_robust_list
	.long sys_splice
	.long sys_sync_file_range
	.long sys_tee			/* 315 */
	.long sys_vmsplice
	.long sys_move_pages
	.long sys_getcpu
	.long sys_epoll_pwait
	.long sys_utimensat		/* 320 */
	.long sys_signalfd
	.long sys_timerfd_create
	.long sys_eventfd
	.long sys_fallocate
	.long sys_timerfd_settime	/* 325 */
	.long sys_timerfd_gettime
	.long sys_signalfd4
	.long sys_eventfd2
	.long sys_epoll_create1
	.long sys_dup3			/* 330 */
	.long sys_pipe2
	.long sys_inotify_init1
	.long sys_preadv
	.long sys_pwritev
	.long sys_rt_tgsigqueueinfo	/* 335 */
	.long sys_perf_event_open
	.long sys_setns

syscall_table_size = (. - sys_call_table)
