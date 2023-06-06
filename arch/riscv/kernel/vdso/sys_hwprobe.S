/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023 Rivos, Inc */

#include <linux/linkage.h>
#include <asm/unistd.h>

.text
ENTRY(riscv_hwprobe)
	.cfi_startproc
	li a7, __NR_riscv_hwprobe
	ecall
	ret

	.cfi_endproc
ENDPROC(riscv_hwprobe)
