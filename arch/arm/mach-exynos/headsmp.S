/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Cloned from linux/arch/arm/mach-realview/headsmp.S
 *
 *  Copyright (c) 2003 ARM Limited
 *  All Rights Reserved
 */
#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>

/*
 * exynos4 specific entry point for secondary CPUs.  This provides
 * a "holding pen" into which all secondary cores are held until we're
 * ready for them to initialise.
 */
ENTRY(exynos4_secondary_startup)
ARM_BE8(setend	be)
	mrc	p15, 0, r0, c0, c0, 5
	and	r0, r0, #15
	adr	r4, 1f
	ldmia	r4, {r5, r6}
	sub	r4, r4, r5
	add	r6, r6, r4
pen:	ldr	r7, [r6]
	cmp	r7, r0
	bne	pen

	/*
	 * we've been released from the holding pen: secondary_stack
	 * should now contain the SVC stack for this core
	 */
	b	secondary_startup
ENDPROC(exynos4_secondary_startup)

	.align 2
1:	.long	.
	.long	pen_release
