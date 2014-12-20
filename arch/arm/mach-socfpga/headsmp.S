/*
 *  Copyright (c) 2003 ARM Limited
 *  Copyright (c) u-boot contributors
 *  Copyright (c) 2012 Pavel Machek <pavel@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>

	.arch	armv7-a

ENTRY(secondary_trampoline)
	/* CPU1 will always fetch from 0x0 when it is brought out of reset.
	 * Thus, we can just subtract the PAGE_OFFSET to get the physical
	 * address of &cpu1start_addr. This would not work for platforms
	 * where the physical memory does not start at 0x0.
	 */
	adr	r0, 1f
	ldmia	r0, {r1, r2}
	sub	r2, r2, #PAGE_OFFSET
	ldr	r3, [r2]
	ldr	r4, [r3]
	bx	r4

	.align
1:	.long	.
	.long	socfpga_cpu1start_addr
ENTRY(secondary_trampoline_end)

ENTRY(socfpga_secondary_startup)
       bl      v7_invalidate_l1
       b       secondary_startup
ENDPROC(socfpga_secondary_startup)
