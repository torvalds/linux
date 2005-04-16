/* linux/arch/arm/mach-s3c2410/sleep.S
 *
 * Copyright (c) 2004 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 Power Manager (Suspend-To-RAM) support
 *
 * Based on PXA/SA1100 sleep code by:
 *	Nicolas Pitre, (c) 2002 Monta Vista Software Inc
 *	Cliff Brake, (c) 2001
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/config.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/hardware.h>
#include <asm/arch/map.h>

#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-mem.h>
#include <asm/arch/regs-serial.h>

/* CONFIG_DEBUG_RESUME is dangerous if your bootloader does not
 * reset the UART configuration, only enable if you really need this!
*/
//#define CONFIG_DEBUG_RESUME

	.text

	/* s3c2410_cpu_suspend
	 *
	 * put the cpu into sleep mode
	 *
	 * entry:
	 *	r0 = sleep save block
	*/

ENTRY(s3c2410_cpu_suspend)
	stmfd	sp!, { r4 - r12, lr }

	@@ store co-processor registers

	mrc	p15, 0, r4, c15, c1, 0	@ CP access register
	mrc	p15, 0, r5, c13, c0, 0	@ PID
	mrc	p15, 0, r6, c3, c0, 0	@ Domain ID
	mrc	p15, 0, r7, c2, c0, 0	@ translation table base address
	mrc	p15, 0, r8, c2, c0, 0	@ auxiliary control register
	mrc	p15, 0, r9, c1, c0, 0	@ control register

	stmia	r0, { r4 - r13 }

	@@ flush the caches to ensure everything is back out to
	@@ SDRAM before the core powers down

	bl	arm920_flush_kern_cache_all

	@@ prepare cpu to sleep

	ldr	r4, =S3C2410_REFRESH
	ldr	r5, =S3C2410_MISCCR
	ldr	r6, =S3C2410_CLKCON
	ldr	r7, [ r4 ]		@ get REFRESH (and ensure in TLB)
	ldr	r8, [ r5 ]		@ get MISCCR (and ensure in TLB)
	ldr	r9, [ r6 ]		@ get CLKCON (and ensure in TLB)

	orr	r7, r7, #S3C2410_REFRESH_SELF	@ SDRAM sleep command
	orr	r8, r8, #S3C2410_MISCCR_SDSLEEP @ SDRAM power-down signals
	orr	r9, r9, #S3C2410_CLKCON_POWER	@ power down command

	teq	pc, #0			@ first as a trial-run to load cache
	bl	s3c2410_do_sleep
	teq	r0, r0			@ now do it for real
	b	s3c2410_do_sleep	@

	@@ align next bit of code to cache line
	.align	8
s3c2410_do_sleep:
	streq	r7, [ r4 ]			@ SDRAM sleep command
	streq	r8, [ r5 ]			@ SDRAM power-down config
	streq	r9, [ r6 ]			@ CPU sleep
1:	beq	1b
	mov	pc, r14

	@@ return to the caller, after having the MMU
	@@ turned on, this restores the last bits from the
	@@ stack
resume_with_mmu:
	ldmfd	sp!, { r4 - r12, pc }

	.ltorg

	@@ the next bits sit in the .data segment, even though they
	@@ happen to be code... the s3c2410_sleep_save_phys needs to be
	@@ accessed by the resume code before it can restore the MMU.
	@@ This means that the variable has to be close enough for the
	@@ code to read it... since the .text segment needs to be RO,
	@@ the data segment can be the only place to put this code.

	.data

	.global	s3c2410_sleep_save_phys
s3c2410_sleep_save_phys:
	.word	0

	/* s3c2410_cpu_resume
	 *
	 * resume code entry for bootloader to call
	 *
	 * we must put this code here in the data segment as we have no
	 * other way of restoring the stack pointer after sleep, and we
	 * must not write to the code segment (code is read-only)
	*/

ENTRY(s3c2410_cpu_resume)
	mov	r0, #PSR_I_BIT | PSR_F_BIT | MODE_SVC
	msr	cpsr_c, r0

	@@ load UART to allow us to print the two characters for
	@@ resume debug

	mov	r2, #S3C2410_PA_UART & 0xff000000
	orr	r2, r2, #S3C2410_PA_UART & 0xff000

#if 0
	/* SMDK2440 LED set */
	mov	r14, #S3C2410_PA_GPIO
	ldr	r12, [ r14, #0x54 ]
	bic	r12, r12, #3<<4
	orr	r12, r12, #1<<7
	str	r12, [ r14, #0x54 ]
#endif

#ifdef CONFIG_DEBUG_RESUME
	mov	r3, #'L'
	strb	r3, [ r2, #S3C2410_UTXH ]
1001:
	ldrb	r14, [ r3, #S3C2410_UTRSTAT ]
	tst	r14, #S3C2410_UTRSTAT_TXE
	beq	1001b
#endif /* CONFIG_DEBUG_RESUME */

	mov	r1, #0
	mcr	p15, 0, r1, c8, c7, 0		@@ invalidate I & D TLBs
	mcr	p15, 0, r1, c7, c7, 0		@@ invalidate I & D caches

	ldr	r0, s3c2410_sleep_save_phys	@ address of restore block
	ldmia	r0, { r4 - r13 }

	mcr	p15, 0, r4, c15, c1, 0		@ CP access register
	mcr	p15, 0, r5, c13, c0, 0		@ PID
	mcr	p15, 0, r6, c3, c0, 0		@ Domain ID
	mcr	p15, 0, r7, c2, c0, 0		@ translation table base
	mcr	p15, 0, r8, c1, c1, 0		@ auxilliary control

#ifdef CONFIG_DEBUG_RESUME
	mov	r3, #'R'
	strb	r3, [ r2, #S3C2410_UTXH ]
#endif

	ldr	r2, =resume_with_mmu
	mcr	p15, 0, r9, c1, c0, 0		@ turn on MMU, etc
	nop					@ second-to-last before mmu
	mov	pc, r2				@ go back to virtual address

	.ltorg
