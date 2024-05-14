/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 SiFive
 */

#include <linux/linkage.h>
#include <asm/unistd.h>

	.text
/* int __vdso_flush_icache(void *start, void *end, unsigned long flags); */
ENTRY(__vdso_flush_icache)
	.cfi_startproc
#ifdef CONFIG_SMP
	li a7, __NR_riscv_flush_icache
	ecall
#else
	fence.i
	li a0, 0
#endif
	ret
	.cfi_endproc
ENDPROC(__vdso_flush_icache)
