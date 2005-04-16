/*
 * Low-level PXA250/210 sleep/wakeUp support
 *
 * Initial SA1110 code:
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * Adapted for PXA by Nicolas Pitre:
 * Copyright (c) 2002 Monta Vista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 */

#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>

#include <asm/arch/pxa-regs.h>

		.text

/*
 * pxa_cpu_suspend()
 *
 * Forces CPU into sleep state
 */

ENTRY(pxa_cpu_suspend)

	mra	r2, r3, acc0
	stmfd	sp!, {r2 - r12, lr}		@ save registers on stack

	@ get coprocessor registers
	mrc	p14, 0, r3, c6, c0, 0		@ clock configuration, for turbo mode
	mrc	p15, 0, r4, c15, c1, 0		@ CP access reg
	mrc	p15, 0, r5, c13, c0, 0		@ PID
	mrc 	p15, 0, r6, c3, c0, 0		@ domain ID
	mrc 	p15, 0, r7, c2, c0, 0		@ translation table base addr
	mrc	p15, 0, r8, c1, c1, 0           @ auxiliary control reg
	mrc 	p15, 0, r9, c1, c0, 0		@ control reg

	bic	r3, r3, #2			@ clear frequency change bit

	@ store them plus current virtual stack ptr on stack
	mov	r10, sp
	stmfd	sp!, {r3 - r10}

	@ preserve phys address of stack
	mov	r0, sp
	bl	sleep_phys_sp
	ldr	r1, =sleep_save_sp
	str	r0, [r1]

	@ clean data cache
	bl	xscale_flush_kern_cache_all

	@ Put the processor to sleep
	@ (also workaround for sighting 28071)

	@ prepare value for sleep mode
	mov	r1, #3				@ sleep mode

	@ prepare to put SDRAM into self-refresh manually
	ldr	r4, =MDREFR
	ldr	r5, [r4]
	orr	r5, r5, #MDREFR_SLFRSH

	@ prepare pointer to physical address 0 (virtual mapping in generic.c)
	mov	r2, #UNCACHED_PHYS_0

	@ Intel PXA255 Specification Update notes problems
	@ about suspending with PXBus operating above 133MHz
	@ (see Errata 31, GPIO output signals, ... unpredictable in sleep
	@
	@ We keep the change-down close to the actual suspend on SDRAM
	@ as possible to eliminate messing about with the refresh clock
	@ as the system will restore with the original speed settings
	@
	@ Ben Dooks, 13-Sep-2004

	ldr	r6, =CCCR
	ldr	r8, [r6]		@ keep original value for resume

	@ ensure x1 for run and turbo mode with memory clock
	bic	r7, r8, #CCCR_M_MASK | CCCR_N_MASK
	orr	r7, r7, #(1<<5) | (2<<7)

	@ check that the memory frequency is within limits
	and	r14, r7, #CCCR_L_MASK
	teq	r14, #1
	bicne	r7, r7, #CCCR_L_MASK
	orrne	r7, r7, #1			@@ 99.53MHz

	@ get ready for the change

	@ note, turbo is not preserved over sleep so there is no
	@ point in preserving it here. we save it on the stack with the
	@ other CP registers instead.
	mov	r0, #0
	mcr	p14, 0, r0, c6, c0, 0
	orr	r0, r0, #2			@ initiate change bit

	@ align execution to a cache line
	b	1f

	.ltorg
	.align	5
1:

	@ All needed values are now in registers.
	@ These last instructions should be in cache

	@ initiate the frequency change...
	str	r7, [r6]
	mcr	p14, 0, r0, c6, c0, 0

	@ restore the original cpu speed value for resume
	str	r8, [r6]

	@ put SDRAM into self-refresh
	str	r5, [r4]

	@ force address lines low by reading at physical address 0
	ldr	r3, [r2]

	@ enter sleep mode
	mcr	p14, 0, r1, c7, c0, 0

20:	b	20b				@ loop waiting for sleep

/*
 * cpu_pxa_resume()
 *
 * entry point from bootloader into kernel during resume
 *
 * Note: Yes, part of the following code is located into the .data section.
 *       This is to allow sleep_save_sp to be accessed with a relative load
 *       while we can't rely on any MMU translation.  We could have put
 *       sleep_save_sp in the .text section as well, but some setups might
 *       insist on it to be truly read-only.
 */

	.data
	.align 5
ENTRY(pxa_cpu_resume)
	mov	r0, #PSR_I_BIT | PSR_F_BIT | MODE_SVC	@ set SVC, irqs off
	msr	cpsr_c, r0

	ldr	r0, sleep_save_sp		@ stack phys addr
	ldr	r2, =resume_after_mmu		@ its absolute virtual address
	ldmfd	r0, {r3 - r9, sp}		@ CP regs + virt stack ptr

	mov	r1, #0
	mcr	p15, 0, r1, c8, c7, 0   	@ invalidate I & D TLBs
	mcr	p15, 0, r1, c7, c7, 0		@ invalidate I & D caches, BTB

#ifdef CONFIG_XSCALE_CACHE_ERRATA
	bic     r9, r9, #0x0004			@ see cpu_xscale_proc_init
#endif

	mcr	p14, 0, r3, c6, c0, 0		@ clock configuration, turbo mode.
	mcr	p15, 0, r4, c15, c1, 0		@ CP access reg
	mcr	p15, 0, r5, c13, c0, 0		@ PID
	mcr 	p15, 0, r6, c3, c0, 0		@ domain ID
	mcr 	p15, 0, r7, c2, c0, 0		@ translation table base addr
	mcr	p15, 0, r8, c1, c1, 0           @ auxiliary control reg
	b	resume_turn_on_mmu		@ cache align execution

	.align 5
resume_turn_on_mmu:
	mcr 	p15, 0, r9, c1, c0, 0		@ turn on MMU, caches, etc.

	@ Let us ensure we jump to resume_after_mmu only when the mcr above
	@ actually took effect.  They call it the "cpwait" operation.
	mrc	p15, 0, r1, c2, c0, 0		@ queue a dependency on CP15
	sub	pc, r2, r1, lsr #32		@ jump to virtual addr
	nop
	nop
	nop

sleep_save_sp:
	.word	0				@ preserve stack phys ptr here

	.text
resume_after_mmu:
#ifdef CONFIG_XSCALE_CACHE_ERRATA
	bl	cpu_xscale_proc_init
#endif
	ldmfd	sp!, {r2, r3}
	mar	acc0, r2, r3
	ldmfd	sp!, {r4 - r12, pc}		@ return to caller


