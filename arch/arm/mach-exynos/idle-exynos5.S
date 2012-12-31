/* linux/arch/arm/mach-exynos/idle-exynos5.S
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS5 AFTR/LPA idle support
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
#include <plat/map-base.h>
#include <plat/map-s5p.h>
#include <mach/smc.h>

	.text

	/*
	 * exynos5_enter_lp
	 *
	 * entry:
	 *	r1 = v:p offset
	 */

ENTRY(exynos5_enter_lp)
	stmfd	sp!, { r3 - r12, lr }

	adr	r0, sleep_save_misc

#ifdef CONFIG_ARM_TRUSTZONE
	mrc	p15, 0, r2, c1, c0, 1	@ read aux control register
	str	r2, [r0], #4
#endif
	mrc	p15, 1, r2, c9, c0, 2	@ read l2 control register
	str	r2, [r0], #4
	mrc	p15, 1, r2, c15, c0, 3	@ read l2 prefetch register
	str	r2, [r0], #4

	ldr	r3, =resume_with_mmu
	bl	cpu_suspend

	bl	exynos5_L1_dcache_flush

	adr	r0, sys_pwr_conf_addr
	ldr	r1, [r0]
	ldr	r2, [r1]
	bic	r2, r2, #(1<<16)
	str	r2, [r1]

#ifdef CONFIG_ARM_TRUSTZONE
        ldr     r0, =SMC_CMD_CPU0AFTR
        mov     r1, #0
        mov     r2, #0
        mov     r3, #0
        smc     0
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
	adr	r4, sleep_save_misc

#ifdef CONFIG_ARM_TRUSTZONE
        mov	r3, #0

        ldr	r0, =SMC_CMD_REG
	ldr	r1, =SMC_REG_ID_CP15(1, 0, 0, 1)	@ aux control register
	ldr	r2, [r4], #4
	smc	0
        ldr	r0, =SMC_CMD_REG
	ldr	r1, =SMC_REG_ID_CP15(9, 1, 0, 2)	@ L2 control register
	ldr	r2, [r4], #4
	smc	0
        ldr	r0, =SMC_CMD_REG
	ldr	r1, =SMC_REG_ID_CP15(15, 1, 0, 3)	@ L2 prefetch register
	ldr	r2, [r4], #4
	smc	0
#else
	ldr	r2, [r4], #4
	mcr	p15, 1, r2, c9, c0, 2			@ L2 control register
	ldr	r2, [r4], #4
	mcr	p15, 1, r2, c15, c0, 3			@ L2 prefetch register
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
	.long	0

	.global sys_pwr_conf_addr
sys_pwr_conf_addr:
	.long	0

	/*
	 * exynos5_L1_dcache_flush
	 *
	 * L1 only dcache flush function
	 *
	 * When enter lowpower cpuidle mode, It is need to L1 only flush function.
	 */
ENTRY(exynos5_L1_dcache_flush)
	dmb					@ ensure ordering with previous memory accesses
	mrc	p15, 1, r0, c0, c0, 1		@ read clidr
	ands	r3, r0, #0x7000000		@ extract loc from clidr
	mov	r3, r3, lsr #23			@ left align loc bit field
	beq	skip				@ if loc is 0, then no need to clean
	mov	r10, #0				@ start clean at cache level 0
loop1:
	add	r2, r10, r10, lsr #1		@ work out 3x current cache level
	mov	r1, r0, lsr r2			@ extract cache type bits from clidr
	and	r1, r1, #7			@ mask of the bits for current cache only
	cmp	r1, #2				@ see what cache we have at this level
	blt	skip				@ skip if no cache, or just i-cache
	mcr	p15, 2, r10, c0, c0, 0		@ select current cache level in cssr
	isb					@ isb to sych the new cssr&csidr
	mrc	p15, 1, r1, c0, c0, 0		@ read the new csidr
	and	r2, r1, #7			@ extract the length of the cache lines
	add	r2, r2, #4			@ add 4 (line length offset)
	ldr	r4, =0x3ff
	ands	r4, r4, r1, lsr #3		@ find maximum number on the way size
	clz	r5, r4				@ find bit position of way size increment
	ldr	r7, =0x7fff
	ands	r7, r7, r1, lsr #13		@ extract max number of the index size
loop2:
	mov	r9, r4				@ create working copy of max way size
loop3:
 ARM(	orr	r11, r10, r9, lsl r5	)	@ factor way and cache number into r11
 THUMB(	lsl	r6, r9, r5		)
 THUMB(	orr	r11, r10, r6		)	@ factor way and cache number into r11
 ARM(	orr	r11, r11, r7, lsl r2	)	@ factor index number into r11
 THUMB(	lsl	r6, r7, r2		)
 THUMB(	orr	r11, r11, r6		)	@ factor index number into r11
	mcr	p15, 0, r11, c7, c14, 2		@ clean & invalidate by set/way
	subs	r9, r9, #1			@ decrement the way
	bge	loop3
	subs	r7, r7, #1			@ decrement the index
	bge	loop2
skip:
	mov	r10, #0				@ swith back to cache level 0
	mcr	p15, 2, r10, c0, c0, 0		@ select current cache level in cssr
	dsb
	isb
	mov	pc, lr
ENDPROC(exynos5_L1_dcache_flush)

	/*
	 * exynos5_idle_resume
	 *
	 * resume code entry for IROM to call
	 *
	 * we must put this code here in the data segment as we have no
	 * other way of restoring the stack pointer after sleep, and we
	 * must not write to the code segment (code is read-only)
	 */

ENTRY(exynos5_idle_resume)
	/*
	 * To use JTEG after wakeup from power mode
	 * Set DBGEN, NIDEN, SPIDEN, SPNIDEN on TZPC1
	 */
	ldr	r0, =0x10110810
	mov	r1, #0xf
	str	r1, [r0]
	dsb
	isb

	b	cpu_resume
