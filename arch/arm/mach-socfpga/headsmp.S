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

	__CPUINIT
	.arch	armv7-a

#define CPU1_START_ADDR 	        0xffd08010

ENTRY(secondary_trampoline)
	movw	r0, #:lower16:CPU1_START_ADDR
	movt  r0, #:upper16:CPU1_START_ADDR

	ldr	r1, [r0]
	bx	r1

ENTRY(secondary_trampoline_end)
