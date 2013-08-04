/*
 * SMP support for R-Mobile / SH-Mobile
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2010  Takashi Yoshii
 *
 * Based on vexpress, Copyright (c) 2003 ARM Limited, All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>

ENTRY(shmobile_invalidate_start)
	bl	v7_invalidate_l1
	b	secondary_startup
ENDPROC(shmobile_invalidate_start)

/*
 * Reset vector for secondary CPUs.
 * This will be mapped at address 0 by SBAR register.
 * We need _long_ jump to the physical address.
 */
	.arm
	.align  12
ENTRY(shmobile_boot_vector)
	ldr     r0, 2f
	ldr     r1, 1f
	bx	r1

ENDPROC(shmobile_boot_vector)

	.align	2
	.globl	shmobile_boot_fn
shmobile_boot_fn:
1:	.space	4
	.globl	shmobile_boot_arg
shmobile_boot_arg:
2:	.space	4
