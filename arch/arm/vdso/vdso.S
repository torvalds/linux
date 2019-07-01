/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Adapted from arm64 version.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/const.h>
#include <asm/page.h>

	.globl vdso_start, vdso_end
	.section .data..ro_after_init
	.balign PAGE_SIZE
vdso_start:
	.incbin "arch/arm/vdso/vdso.so"
	.balign PAGE_SIZE
vdso_end:

	.previous
