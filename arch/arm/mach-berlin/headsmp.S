/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014 Marvell Techanallogy Group Ltd.
 *
 * Antoine Ténart <antoine.tenart@free-electrons.com>
 */

#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/assembler.h>

/*
 * If the following instruction is set in the reset exception vector, CPUs
 * will fetch the value of the software reset address vector when being
 * reset.
 */
.global boot_inst
boot_inst:
	ldr	pc, [pc, #140]

	.align
