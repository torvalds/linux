/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 SiFive
 */

#include <linux/linkage.h>
#include <asm/unistd.h>
#include <asm/assembler.h>

	.text
/* int __vdso_getcpu(unsigned *cpu, unsigned *node, void *unused); */
SYM_FUNC_START(__vdso_getcpu)
	.cfi_startproc
	vdso_lpad
	/* For now, just do the syscall. */
	li a7, __NR_getcpu
	ecall
	ret
	.cfi_endproc
SYM_FUNC_END(__vdso_getcpu)

emit_riscv_feature_1_and
