/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rivos Inc.
 */

.text
.global test_sub16
test_sub16:
	lh	a0, sub16
	addi	a0, a0, -32
	ret
first:
	.space 32
second:

.data
sub16:
	.reloc		sub16, R_RISCV_ADD16, second
	.reloc		sub16, R_RISCV_SUB16, first
	.half		0
