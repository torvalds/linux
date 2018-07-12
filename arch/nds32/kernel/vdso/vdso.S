// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2012 ARM Limited
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/const.h>
#include <asm/page.h>

	.globl vdso_start, vdso_end
	.section .rodata
	.balign PAGE_SIZE
vdso_start:
	.incbin "arch/nds32/kernel/vdso/vdso.so"
	.balign PAGE_SIZE
vdso_end:

	.previous
