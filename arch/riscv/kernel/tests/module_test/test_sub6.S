/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rivos Inc.
 */

.text
.global test_sub6
test_sub6:
	lb	a0, sub6
	addi	a0, a0, -32
	ret
first:
	.space 32
second:

.data
sub6:
	.reloc		sub6, R_RISCV_SET6, second
	.reloc		sub6, R_RISCV_SUB6, first
	.byte		0
