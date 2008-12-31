/*
 * SA11x0 Assembler Sleep/WakeUp Management Routines
 *
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 *
 * History:
 *
 * 2001-02-06: Cliff Brake         Initial code
 *
 * 2001-08-29:	Nicolas Pitre	Simplified.
 *
 * 2002-05-27:	Nicolas Pitre	Revisited, more cleanup and simplification.
 *				Storage is on the stack now.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/hardware.h>



		.text



/*
 * sa1100_cpu_suspend()
 *
 * Causes sa11x0 to enter sleep state
 *
 */

ENTRY(sa1100_cpu_suspend)

	stmfd	sp!, {r4 - r12, lr}		@ save registers on stack

	@ get coprocessor registers
	mrc 	p15, 0, r4, c3, c0, 0		@ domain ID
	mrc 	p15, 0, r5, c2, c0, 0		@ translation table base addr
	mrc	p15, 0, r6, c13, c0, 0		@ PID
	mrc 	p15, 0, r7, c1, c0, 0		@ control reg

	@ store them plus current virtual stack ptr on stack
	mov	r8, sp
	stmfd	sp!, {r4 - r8}

	@ preserve phys address of stack
	mov	r0, sp
	bl	sleep_phys_sp
	ldr	r1, =sleep_save_sp
	str	r0, [r1]

	@ clean data cache and invalidate WB
	bl	v4wb_flush_kern_cache_all

	@ disable clock switching
	mcr	p15, 0, r1, c15, c2, 2

        @ Adjust memory timing before lowering CPU clock
	@ Clock speed adjustment without changing memory timing makes
	@ CPU hang in some cases
        ldr     r0, =MDREFR
        ldr     r1, [r0]
        orr     r1, r1, #MDREFR_K1DB2
        str     r1, [r0]

	@ delay 90us and set CPU PLL to lowest speed
	@ fixes resume problem on high speed SA1110
	mov	r0, #90
	bl	__udelay
	ldr	r0, =PPCR
	mov	r1, #0
	str	r1, [r0]
	mov	r0, #90
	bl	__udelay

	/*
	 * SA1110 SDRAM controller workaround.  register values:
	 *
	 * r0  = &MSC0
	 * r1  = &MSC1
	 * r2  = &MSC2
	 * r3  = MSC0 value
	 * r4  = MSC1 value
	 * r5  = MSC2 value
	 * r6  = &MDREFR
	 * r7  = first MDREFR value
	 * r8  = second MDREFR value
	 * r9  = &MDCNFG
	 * r10 = MDCNFG value
	 * r11 = third MDREFR value
	 * r12 = &PMCR
	 * r13 = PMCR value (1)
	 */

	ldr	r0, =MSC0
	ldr	r1, =MSC1
	ldr	r2, =MSC2

	ldr	r3, [r0]
	bic	r3, r3, #FMsk(MSC_RT)
	bic	r3, r3, #FMsk(MSC_RT)<<16

	ldr	r4, [r1]
	bic	r4, r4, #FMsk(MSC_RT)
	bic	r4, r4, #FMsk(MSC_RT)<<16

	ldr	r5, [r2]
	bic	r5, r5, #FMsk(MSC_RT)
	bic	r5, r5, #FMsk(MSC_RT)<<16

	ldr	r6, =MDREFR

	ldr	r7, [r6]
bic	r7, r7, #0x0000FF00
bic	r7, r7, #0x000000F0
orr	r8, r7, #MDREFR_SLFRSH

	ldr	r9, =MDCNFG
	ldr	r10, [r9]
	bic	r10, r10, #(MDCNFG_DE0+MDCNFG_DE1)
	bic	r10, r10, #(MDCNFG_DE2+MDCNFG_DE3)

	bic	r11, r8, #MDREFR_SLFRSH
	bic	r11, r11, #MDREFR_E1PIN

	ldr	r12, =PMCR

	mov	r13, #PMCR_SF

	b	sa1110_sdram_controller_fix

	.align 5
sa1110_sdram_controller_fix:

	@ Step 1 clear RT field of all MSCx registers
	str 	r3, [r0]
	str	r4, [r1]
	str	r5, [r2]

	@ Step 2 clear DRI field in MDREFR
	str	r7, [r6]

	@ Step 3 set SLFRSH bit in MDREFR
	str	r8, [r6]

	@ Step 4 clear DE bis in MDCNFG
	str	r10, [r9]

	@ Step 5 clear DRAM refresh control register
	str	r11, [r6]

	@ Wow, now the hardware suspend request pins can be used, that makes them functional for
	@ about 7 ns out of the	entire time that the CPU is running!

	@ Step 6 set force sleep bit in PMCR

	str	r13, [r12]

20:	b	20b			@ loop waiting for sleep

/*
 * cpu_sa1100_resume()
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
ENTRY(sa1100_cpu_resume)
	mov	r0, #PSR_F_BIT | PSR_I_BIT | SVC_MODE
	msr	cpsr_c, r0			@ set SVC, irqs off

	ldr	r0, sleep_save_sp		@ stack phys addr
	ldr	r2, =resume_after_mmu		@ its absolute virtual address
	ldmfd	r0, {r4 - r7, sp}		@ CP regs + virt stack ptr

	mov	r1, #0
	mcr	p15, 0, r1, c8, c7, 0   	@ flush I+D TLBs
	mcr	p15, 0, r1, c7, c7, 0		@ flush I&D cache
	mcr	p15, 0, r1, c9, c0, 0		@ invalidate RB
	mcr	p15, 0, r1, c9, c0, 5		@ allow user space to use RB

	mcr	p15, 0, r4, c3, c0, 0		@ domain ID
	mcr	p15, 0, r5, c2, c0, 0		@ translation table base addr
	mcr	p15, 0, r6, c13, c0, 0		@ PID
	b	resume_turn_on_mmu		@ cache align execution

	.align 5
resume_turn_on_mmu:
	mcr 	p15, 0, r7, c1, c0, 0		@ turn on MMU, caches, etc.
	nop
	mov	pc, r2				@ jump to virtual addr
	nop
	nop
	nop

sleep_save_sp:
	.word	0				@ preserve stack phys ptr here

	.text
resume_after_mmu:
	mcr	p15, 0, r1, c15, c1, 2		@ enable clock switching
	ldmfd	sp!, {r4 - r12, pc}		@ return to caller


