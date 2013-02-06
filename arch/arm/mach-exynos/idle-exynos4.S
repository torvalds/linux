/* linux/arch/arm/mach-exynos/idle-exynos4.S
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4210 AFTR/LPA idle support
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
#include <asm/hardware/cache-l2x0.h>
#include <plat/map-base.h>
#include <plat/map-s5p.h>
#include <mach/smc.h>
#include <mach/map-exynos4.h>

	.text

	/*
	 * exynos4_enter_lp
	 *
	 * entry:
	 *	r1 = v:p offset
	 */

ENTRY(exynos4_enter_lp)
	stmfd	sp!, { r3 - r12, lr }

	adr	r0, sleep_save_misc

	mrc	p15, 0, r2, c15, c0, 0	@ read power control register
	str	r2, [r0], #4

	mrc	p15, 0, r2, c15, c0, 1	@ read diagnostic register
	str	r2, [r0], #4

	ldr	r3, =resume_with_mmu
	bl	cpu_suspend

	mov	r0, sp

	mrc	p15, 0, r1, c2, c0, 0	@ TTB 0
	mov	r2, r1, lsr #14		@ get TTB0 base
	mov	r1, r2, lsl #14
	bl	exynos4_flush_cache

	adr	r0, sys_pwr_conf_addr
	ldr	r1, [r0]
	ldr	r2, [r1]
	bic	r2, r2, #(1<<16)
	str	r2, [r1]

#ifdef CONFIG_ARM_TRUSTZONE
	ldr	r0, =SMC_CMD_CPU0AFTR
	mov	r1, #0
	mov	r2, #0
	mov	r3, #0
#ifdef REQUIRES_SEC
	.arch_extension sec
#endif
	smc	0
#else
	dsb
	wfi
#endif

	/* Restore original sp */
	mov	r0, sp
	add	r0, r0, #4
	ldr	sp, [r0]

	mov	r0, #0
	b	early_wakeup

resume_with_mmu:
#ifdef CONFIG_CACHE_L2X0
	/* Enable L2 cache */
#ifdef CONFIG_ARM_TRUSTZONE
	ldr	r0, =SMC_CMD_L2X0CTRL
	mov	r1, #1
	mov	r2, #0
	mov	r3, #0
#ifdef REQUIRES_SEC
	.arch_extension sec
#endif
	smc	0
#else
	mov	r0, #1
	ldr	r1, =S5P_VA_L2CC
	str	r0, [r1, #0x100]
#endif
#endif
	adr	r0, sleep_save_misc

#ifdef CONFIG_ARM_TRUSTZONE
	ldr	r1, [r0], #4
	ldr	r2, [r0], #4
	ldr	r0, =SMC_CMD_C15RESUME
	mov	r3, #0
#ifdef REQUIRES_SEC
	.arch_extension sec
#endif
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

	.global sys_pwr_conf_addr
sys_pwr_conf_addr:
	.long	0

	.global l2x0_save
l2x0_save:
	.word	0
	.word	0
	.word	0

	.global scu_save
scu_save:
	.word	0
	.word	0

	/*
	 * exynos4_idle_resume
	 *
	 * resume code entry for IROM to call
	 *
	 * we must put this code here in the data segment as we have no
	 * other way of restoring the stack pointer after sleep, and we
	 * must not write to the code segment (code is read-only)
	 */

ENTRY(exynos4_idle_resume)
	/* SCU enable */
	ldr	r1, =0x10500000
	adr	r0, scu_save

	ldr	r5, [r0]
	ldr	r6, [r0, #4]

	str	r5, [r1, #0x30]
	str	r6, [r1]

#ifdef CONFIG_CACHE_L2X0
	/* Restore register value for L2X0 */
	adr	r0, l2x0_save

	ldr	r5, [r0], #4	@ Data latency
	ldr	r6, [r0], #4	@ Tag latency
	ldr	r7, [r0], #4	@ prepatch

#ifdef CONFIG_ARM_TRUSTZONE
	mov	r1, r5
	mov	r2, r6
	mov	r3, r7

	ldr	r0, =SMC_CMD_L2X0SETUP1

#ifdef REQUIRES_SEC
	.arch_extension sec
#endif
	smc	0

	ldr	r0, =SMC_CMD_L2X0SETUP2
	ldr	r1, =0x3
	ldr	r2, =0x7C470001
	ldr	r3, =0xC200FFFF

#ifdef REQUIRES_SEC
	.arch_extension sec
#endif
	smc	0
#else
	ldr	r0, =EXYNOS4_PA_L2CC

	str	r5, [r0, #L2X0_TAG_LATENCY_CTRL]
	str	r6, [r0, #L2X0_DATA_LATENCY_CTRL]
	str	r7, [r0, #L2X0_PREFETCH_CTRL]

	/*
	 * Set Power ctrl register for L2X0
	 */
	mov	r1, #0x3
	str	r1, [r0, #L2X0_POWER_CTRL]

	ldr	r1, [r0, #L2X0_AUX_CTRL]
	ldr	r2, =0x7C470001
	ldr	r3, =0xC200FFFF

	and	r1, r1, r3
	orr	r1, r1, r2

	str	r1, [r0, #L2X0_AUX_CTRL]

#endif
#endif
	b	cpu_resume
