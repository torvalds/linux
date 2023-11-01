/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rivos Inc.
 */

.text
.global test_uleb_basic
test_uleb_basic:
	ld	a0, second
	addi	a0, a0, -127
	ret

.global test_uleb_large
test_uleb_large:
	ld	a0, fourth
	addi	a0, a0, -0x07e8
	ret

.data
first:
	.space 127
second:
	.reloc second, R_RISCV_SET_ULEB128, second
	.reloc second, R_RISCV_SUB_ULEB128, first
	.dword 0
third:
	.space 1000
fourth:
	.reloc fourth, R_RISCV_SET_ULEB128, fourth
	.reloc fourth, R_RISCV_SUB_ULEB128, third
	.dword 0
