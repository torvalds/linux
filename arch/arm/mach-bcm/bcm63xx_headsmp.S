/*
 *  Copyright (C) 2015, Broadcom Corporation
 *  All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

ENTRY(bcm63138_secondary_startup)
 ARM_BE8(setend	be)
	/*
	 * L1 cache does have unpredictable contents at power-up clean its
	 * contents without flushing
	 */
	bl      v7_invalidate_l1
	nop

	b	secondary_startup
ENDPROC(bcm63138_secondary_startup)
