/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from RISC-V:
 * Copyright (C) 2014 Regents of the University of California
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/page.h>

	__PAGE_ALIGNED_DATA

	.globl vdso_start, vdso_end
	.balign PAGE_SIZE
vdso_start:
	.incbin "arch/loongarch/vdso/vdso.so"
	.balign PAGE_SIZE
vdso_end:

	.previous
