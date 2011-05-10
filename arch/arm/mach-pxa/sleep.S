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

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/hardware.h>
#include <mach/smemc.h>
#include <mach/pxa2xx-regs.h>

#define MDREFR_KDIV	0x200a4000	// all banks
#define CCCR_SLEEP	0x00000107	// L=7 2N=2 A=0 PPDIS=0 CPDIS=0

		.text

#ifdef CONFIG_PXA3xx
/*
 * pxa3xx_cpu_suspend() - forces CPU into sleep state (S2D3C4)
 *
 * r0 = v:p offset
 */
ENTRY(pxa3xx_cpu_suspend)

#ifndef CONFIG_IWMMXT
	mra	r2, r3, acc0
#endif
	stmfd	sp!, {r2 - r12, lr}	@ save registers on stack
	mov	r1, r0
	ldr	r3, =pxa_cpu_resume	@ resume function
	bl	cpu_suspend

	mov	r0, #0x06		@ S2D3C4 mode
	mcr	p14, 0, r0, c7, c0, 0	@ enter sleep

20:	b	20b			@ waiting for sleep
#endif /* CONFIG_PXA3xx */

#ifdef CONFIG_PXA27x
/*
 * pxa27x_cpu_suspend()
 *
 * Forces CPU into sleep state.
 *
 * r0 = value for PWRMODE M field for desired sleep state
 * r1 = v:p offset
 */
ENTRY(pxa27x_cpu_suspend)

#ifndef CONFIG_IWMMXT
	mra	r2, r3, acc0
#endif
	stmfd	sp!, {r2 - r12, lr}		@ save registers on stack
	mov	r4, r0				@ save sleep mode
	ldr	r3, =pxa_cpu_resume		@ resume function
	bl	cpu_suspend

	@ Put the processor to sleep
	@ (also workaround for sighting 28071)

	@ prepare value for sleep mode
	mov	r1, r4				@ sleep mode

	@ prepare pointer to physical address 0 (virtual mapping in generic.c)
	mov	r2, #UNCACHED_PHYS_0

	@ prepare SDRAM refresh settings
	ldr	r4, =MDREFR
	ldr	r5, [r4]

	@ enable SDRAM self-refresh mode
	orr	r5, r5, #MDREFR_SLFRSH

	@ set SDCLKx divide-by-2 bits (this is part of a workaround for Errata 50)
	ldr	r6, =MDREFR_KDIV
	orr	r5, r5, r6

	@ Intel PXA270 Specification Update notes problems sleeping
	@ with core operating above 91 MHz
	@ (see Errata 50, ...processor does not exit from sleep...)

	ldr	r6, =CCCR
	ldr	r8, [r6]		@ keep original value for resume

	ldr	r7, =CCCR_SLEEP		@ prepare CCCR sleep value
	mov	r0, #0x2		@ prepare value for CLKCFG

	@ align execution to a cache line
	b	pxa_cpu_do_suspend
#endif

#ifdef CONFIG_PXA25x
/*
 * pxa25x_cpu_suspend()
 *
 * Forces CPU into sleep state.
 *
 * r0 = value for PWRMODE M field for desired sleep state
 * r1 = v:p offset
 */

ENTRY(pxa25x_cpu_suspend)
	stmfd	sp!, {r2 - r12, lr}		@ save registers on stack
	mov	r4, r0				@ save sleep mode
	ldr	r3, =pxa_cpu_resume		@ resume function
	bl	cpu_suspend
	@ prepare value for sleep mode
	mov	r1, r4				@ sleep mode

	@ prepare pointer to physical address 0 (virtual mapping in generic.c)
	mov	r2, #UNCACHED_PHYS_0

	@ prepare SDRAM refresh settings
	ldr	r4, =MDREFR
	ldr	r5, [r4]

	@ enable SDRAM self-refresh mode
	orr	r5, r5, #MDREFR_SLFRSH

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
	b	pxa_cpu_do_suspend
#endif

	.ltorg
	.align	5
pxa_cpu_do_suspend:

	@ All needed values are now in registers.
	@ These last instructions should be in cache

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
 * pxa_cpu_resume()
 *
 * entry point from bootloader into kernel during resume
 */
	.align 5
pxa_cpu_resume:
	ldmfd	sp!, {r2, r3}
#ifndef CONFIG_IWMMXT
	mar	acc0, r2, r3
#endif
	ldmfd	sp!, {r4 - r12, pc}		@ return to caller
