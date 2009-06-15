/*
 * Secondary CPU startup routine source file.
 *
 * Copyright (C) 2009 Texas Instruments, Inc.
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

/* Physical address needed since MMU not enabled yet on secondary core */
#define OMAP4_AUX_CORE_BOOT1_PA			0x48281804

	__INIT

/*
 * OMAP4 specific entry point for secondary CPU to jump from ROM
 * code.  This routine also provides a holding flag into which
 * secondary core is held until we're ready for it to initialise.
 * The primary core will update the this flag using a hardware
 * register AuxCoreBoot1.
 */
ENTRY(omap_secondary_startup)
	mrc	p15, 0, r0, c0, c0, 5
	and	r0, r0, #0x0f
hold:	ldr	r1, =OMAP4_AUX_CORE_BOOT1_PA	@ read from AuxCoreBoot1
	ldr	r2, [r1]
	cmp	r2, r0
	bne	hold

	/*
	 * we've been released from the cpu_release,secondary_stack
	 * should now contain the SVC stack for this core
	 */
	b	secondary_startup

