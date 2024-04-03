/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rivos Inc.
 */

.text
.global test_sub32
test_sub32:
	lw	a0, sub32
	addi	a0, a0, -32
	ret
first:
	.space 32
second:

.data
sub32:
	.reloc		sub32, R_RISCV_ADD32, second
	.reloc		sub32, R_RISCV_SUB32, first
	.word		0
