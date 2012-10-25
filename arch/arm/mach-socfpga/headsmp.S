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

	__INIT
	.arch   armv7-a

ENTRY(secondary_trampoline)
	movw	r2, #:lower16:cpu1start_addr
	movt  r2, #:upper16:cpu1start_addr

	/* The socfpga VT cannot handle a 0xC0000000 page offset when loading
		the cpu1start_addr, we bit clear it. Tested on HW and VT. */
	bic	r2, r2, #0x40000000

	ldr	r0, [r2]
	ldr	r1, [r0]
	bx	r1

ENTRY(secondary_trampoline_end)

ENTRY(socfpga_secondary_startup)
       bl      v7_invalidate_l1
       b       secondary_startup
ENDPROC(socfpga_secondary_startup)
