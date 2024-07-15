/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 Rivos Inc.
 */

.text
.global test_set16
test_set16:
	lw	a0, set16
	la	t0, set16
#ifdef CONFIG_32BIT
	slli	t0, t0, 16
	srli	t0, t0, 16
#else
	slli	t0, t0, 48
	srli	t0, t0, 48
#endif
	sub	a0, a0, t0
	ret
.data
set16:
	.reloc set16, R_RISCV_SET16, set16
	.word 0
