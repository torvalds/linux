/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/const.h>
#include <asm/assembler.h>
#include <asm/page.h>

	.globl vdso_start, vdso_end
	.section .rodata
	.balign PAGE_SIZE
vdso_start:
	.incbin "arch/arm64/kernel/vdso/vdso.so"
	.balign PAGE_SIZE
vdso_end:

	.previous

emit_aarch64_feature_1_and
