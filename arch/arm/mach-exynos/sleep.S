/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Exynos low-level resume code
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
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/hardware/cache-l2x0.h>

#define CPU_MASK	0xff0ffff0
#define CPU_CORTEX_A9	0x410fc090

	/*
	 * The following code is located into the .data section. This is to
	 * allow l2x0_regs_phys to be accessed with a relative load while we
	 * can't rely on any MMU translation. We could have put l2x0_regs_phys
	 * in the .text section as well, but some setups might insist on it to
	 * be truly read-only. (Reference from: arch/arm/kernel/sleep.S)
	 */
	.data
	.align

	/*
	 * sleep magic, to allow the bootloader to check for an valid
	 * image to resume to. Must be the first word before the
	 * exynos_cpu_resume entry.
	 */

	.word	0x2bedf00d

	/*
	 * exynos_cpu_resume
	 *
	 * resume code entry for bootloader to call
	 */

ENTRY(exynos_cpu_resume)
#ifdef CONFIG_CACHE_L2X0
	mrc	p15, 0, r0, c0, c0, 0
	ldr	r1, =CPU_MASK
	and	r0, r0, r1
	ldr	r1, =CPU_CORTEX_A9
	cmp	r0, r1
	bne	skip_l2_resume
	adr	r0, l2x0_regs_phys
	ldr	r0, [r0]
	cmp	r0, #0
	beq	skip_l2_resume
	ldr	r1, [r0, #L2X0_R_PHY_BASE]
	ldr	r2, [r1, #L2X0_CTRL]
	tst	r2, #0x1
	bne	skip_l2_resume
	ldr	r2, [r0, #L2X0_R_AUX_CTRL]
	str	r2, [r1, #L2X0_AUX_CTRL]
	ldr	r2, [r0, #L2X0_R_TAG_LATENCY]
	str	r2, [r1, #L2X0_TAG_LATENCY_CTRL]
	ldr	r2, [r0, #L2X0_R_DATA_LATENCY]
	str	r2, [r1, #L2X0_DATA_LATENCY_CTRL]
	ldr	r2, [r0, #L2X0_R_PREFETCH_CTRL]
	str	r2, [r1, #L2X0_PREFETCH_CTRL]
	ldr	r2, [r0, #L2X0_R_PWR_CTRL]
	str	r2, [r1, #L2X0_POWER_CTRL]
	mov	r2, #1
	str	r2, [r1, #L2X0_CTRL]
skip_l2_resume:
#endif
	b	cpu_resume
ENDPROC(exynos_cpu_resume)
#ifdef CONFIG_CACHE_L2X0
	.globl l2x0_regs_phys
l2x0_regs_phys:
	.long	0
#endif
