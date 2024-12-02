/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * purgatory: Runs between two kernels
 *
 * Copyright (C) 2022 Huawei Technologies Co, Ltd.
 *
 * Author: Li Zhengyu (lizhengyu3@huawei.com)
 *
 */

.macro	size, sym:req
	.size \sym, . - \sym
.endm

.text

.globl purgatory_start
purgatory_start:

	lla	sp, .Lstack
	mv	s0, a0	/* The hartid of the current hart */
	mv	s1, a1	/* Phys address of the FDT image */

	jal	purgatory

	/* Start new image. */
	mv	a0, s0
	mv	a1, s1
	ld	a2, riscv_kernel_entry
	jr	a2

size purgatory_start

.align 4
	.rept	256
	.quad	0
	.endr
.Lstack:

.data

.globl riscv_kernel_entry
riscv_kernel_entry:
	.quad	0
size riscv_kernel_entry

.end
