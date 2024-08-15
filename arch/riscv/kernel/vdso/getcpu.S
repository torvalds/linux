/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 SiFive
 */

#include <linux/linkage.h>
#include <asm/unistd.h>

	.text
/* int __vdso_getcpu(unsigned *cpu, unsigned *node, void *unused); */
ENTRY(__vdso_getcpu)
	.cfi_startproc
	/* For now, just do the syscall. */
	li a7, __NR_getcpu
	ecall
	ret
	.cfi_endproc
ENDPROC(__vdso_getcpu)
