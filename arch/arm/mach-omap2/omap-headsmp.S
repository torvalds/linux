/*
 * Secondary CPU startup routine source file.
 *
 * Copyright (C) 2009-2014 Texas Instruments, Inc.
 *
 * Author:
 *      Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * Interface functions needed for the SMP. This file is based on arm
 * realview smp platform.
 * Copyright (c) 2003 ARM Limited.
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/init.h>

#include "omap44xx.h"

/* Physical address needed since MMU not enabled yet on secondary core */
#define AUX_CORE_BOOT0_PA			0x48281800

/*
 * OMAP5 specific entry point for secondary CPU to jump from ROM
 * code.  This routine also provides a holding flag into which
 * secondary core is held until we're ready for it to initialise.
 * The primary core will update this flag using a hardware
 * register AuxCoreBoot0.
 */
ENTRY(omap5_secondary_startup)
wait:	ldr	r2, =AUX_CORE_BOOT0_PA	@ read from AuxCoreBoot0
	ldr	r0, [r2]
	mov	r0, r0, lsr #5
	mrc	p15, 0, r4, c0, c0, 5
	and	r4, r4, #0x0f
	cmp	r0, r4
	bne	wait
	b	secondary_startup
ENDPROC(omap5_secondary_startup)
/*
 * OMAP4 specific entry point for secondary CPU to jump from ROM
 * code.  This routine also provides a holding flag into which
 * secondary core is held until we're ready for it to initialise.
 * The primary core will update this flag using a hardware
 * register AuxCoreBoot0.
 */
ENTRY(omap4_secondary_startup)
hold:	ldr	r12,=0x103
	dsb
	smc	#0			@ read from AuxCoreBoot0
	mov	r0, r0, lsr #9
	mrc	p15, 0, r4, c0, c0, 5
	and	r4, r4, #0x0f
	cmp	r0, r4
	bne	hold

	/*
	 * we've been released from the wait loop,secondary_stack
	 * should now contain the SVC stack for this core
	 */
	b	secondary_startup
ENDPROC(omap4_secondary_startup)

ENTRY(omap4460_secondary_startup)
hold_2:	ldr	r12,=0x103
	dsb
	smc	#0			@ read from AuxCoreBoot0
	mov	r0, r0, lsr #9
	mrc	p15, 0, r4, c0, c0, 5
	and	r4, r4, #0x0f
	cmp	r0, r4
	bne	hold_2

	/*
	 * GIC distributor control register has changed between
	 * CortexA9 r1pX and r2pX. The Control Register secure
	 * banked version is now composed of 2 bits:
	 * bit 0 == Secure Enable
	 * bit 1 == Non-Secure Enable
	 * The Non-Secure banked register has not changed
	 * Because the ROM Code is based on the r1pX GIC, the CPU1
	 * GIC restoration will cause a problem to CPU0 Non-Secure SW.
	 * The workaround must be:
	 * 1) Before doing the CPU1 wakeup, CPU0 must disable
	 * the GIC distributor
	 * 2) CPU1 must re-enable the GIC distributor on
	 * it's wakeup path.
	 */
	ldr	r1, =OMAP44XX_GIC_DIST_BASE
	ldr	r0, [r1]
	orr	r0, #1
	str	r0, [r1]

	/*
	 * we've been released from the wait loop,secondary_stack
	 * should now contain the SVC stack for this core
	 */
	b	secondary_startup
ENDPROC(omap4460_secondary_startup)
