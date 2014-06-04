/*
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Antoine Ténart <antoine.tenart@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

ENTRY(berlin_secondary_startup)
 ARM_BE8(setend be)
	bl	v7_invalidate_l1
	b       secondary_startup
ENDPROC(berlin_secondary_startup)

/*
 * If the following instruction is set in the reset exception vector, CPUs
 * will fetch the value of the software reset address vector when being
 * reset.
 */
.global boot_inst
boot_inst:
	ldr	pc, [pc, #140]

	.align
