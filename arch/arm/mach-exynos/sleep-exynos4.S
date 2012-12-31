/* linux/arch/arm/mach-exynos/sleep-exynos4.S
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 power Manager (Suspend-To-RAM) support
 * Based on S3C2410 sleep code by:
 *	Ben Dooks, (c) 2004 Simtec Electronics
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

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/memory.h>
#include <mach/smc.h>

	.text

	/*
	 * s3c_cpu_save
	 *
	 * entry:
	 *	r1 = v:p offset
	 */

ENTRY(s3c_cpu_save)

	stmfd	sp!, { r3 - r12, lr }

	adr	r0, sleep_save_misc

	mrc	p15, 0, r2, c15, c0, 0	@ read power control register
	str	r2, [r0], #4

	mrc	p15, 0, r2, c15, c0, 1	@ read diagnostic register
	str	r2, [r0], #4

	ldr	r3, =resume_with_mmu
	bl	cpu_suspend

	bl	exynos4_cpu_suspend

	/* Restore original sp */
	mov	r0, sp
	add	r0, r0, #4
	ldr	sp, [r0]

	mov	r0, #0
	b	early_wakeup

resume_with_mmu:

	adr	r0, sleep_save_misc

#ifdef CONFIG_ARM_TRUSTZONE
	ldr	r1, [r0], #4
	ldr	r2, [r0], #4

	ldr	r0, =SMC_CMD_C15RESUME
	mov	r3, #0
	smc	0
#else
	ldr	r1, [r0], #4
	mcr	p15, 0, r1, c15, c0, 0	@ write power control register

	ldr	r1, [r0], #4
	mcr	p15, 0, r1, c15, c0, 1	@ write diagnostic register
#endif

	mov	r0, #1
early_wakeup:

	ldmfd	sp!, { r3 - r12, pc }

	.ltorg

	/*
	 * sleep magic, to allow the bootloader to check for an valid
	 * image to resume to. Must be the first word before the
	 * s3c_cpu_resume entry.
	 */

	.word	0x2bedf00d

sleep_save_misc:
	.long	0
	.long	0

	/*
	 * s3c_cpu_resume
	 *
	 * resume code entry for bootloader to call
	 *
	 * we must put this code here in the data segment as we have no
	 * other way of restoring the stack pointer after sleep, and we
	 * must not write to the code segment (code is read-only)
	 */

ENTRY(s3c_cpu_resume)
	b	cpu_resume
