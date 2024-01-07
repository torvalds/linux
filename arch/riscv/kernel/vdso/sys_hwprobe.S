/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Rivos, Inc */

#include <linux/linkage.h>
#include <asm/unistd.h>

.text
SYM_FUNC_START(riscv_hwprobe)
	.cfi_startproc
	li a7, __NR_riscv_hwprobe
	ecall
	ret

	.cfi_endproc
SYM_FUNC_END(riscv_hwprobe)
