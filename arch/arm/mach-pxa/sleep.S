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

#ifdef CONFIG_PXA27x			// workaround for Errata 50
#define MDREFR_KDIV	0x200a4000	// all banks
#define CCCR_SLEEP	0x00000107	// L=7 2N=2 A=0 PPDIS=0 CPDIS=0
#endif

		.text

/*
 * pxa_cpu_suspend()
 *
 * Forces CPU into sleep state.
 *
 * r0 = value for PWRMODE M field for desired sleep state
 */

ENTRY(pxa_cpu_suspend)

#ifndef CONFIG_IWMMXT
	mra	r2, r3, acc0
#endif
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

	mov r5, r0				@ save sleep mode
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
	mov	r1, r5				@ sleep mode

	@ prepare pointer to physical address 0 (virtual mapping in generic.c)
	mov	r2, #UNCACHED_PHYS_0

	@ prepare SDRAM refresh settings
	ldr	r4, =MDREFR
	ldr	r5, [r4]

	@ enable SDRAM self-refresh mode
	orr	r5, r5, #MDREFR_SLFRSH

#ifdef CONFIG_PXA27x
	@ set SDCLKx divide-by-2 bits (this is part of a workaround for Errata 50)
	ldr	r6, =MDREFR_KDIV
	orr	r5, r5, r6
#endif

#ifdef CONFIG_PXA25x
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
#endif
#ifdef CONFIG_PXA27x
	@ Intel PXA270 Specification Update notes problems sleeping
	@ with core operating above 91 MHz
	@ (see Errata 50, ...processor does not exit from sleep...)

	ldr	r6, =CCCR
	ldr	r8, [r6]		@ keep original value for resume

	ldr	r7, =CCCR_SLEEP		@ prepare CCCR sleep value
	mov	r0, #0x2		@ prepare value for CLKCFG
#endif

	@ align execution to a cache line
	b	1f

	.ltorg
	.align	5
1:

	@ All needed values are now in registers.
	@ These last instructions should be in cache

#if defined(CONFIG_PXA25x) || defined(CONFIG_PXA27x)
	@ initiate the frequency change...
	str	r7, [r6]
	mcr	p14, 0, r0, c6, c0, 0

	@ restore the original cpu speed value for resume
	str	r8, [r6]

	@ need 6 13-MHz cycles before changing PWRMODE
	@ just set frequency to 91-MHz... 6*91/13 = 42

	mov	r0, #42
10:	subs	r0, r0, #1
	bne	10b
#endif

	@ Do not reorder...
	@ Intel PXA270 Specification Update notes problems performing
	@ external accesses after SDRAM is put in self-refresh mode
	@ (see Errata 39 ...hangs when entering self-refresh mode)

	@ force address lines low by reading at physical address 0
	ldr	r3, [r2]

	@ put SDRAM into self-refresh
	str	r5, [r4]

	@ enter sleep mode
	mcr	p14, 0, r1, c7, c0, 0		@ PWRMODE

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
#ifndef CONFIG_IWMMXT
	mar	acc0, r2, r3
#endif
	ldmfd	sp!, {r4 - r12, pc}		@ return to caller


