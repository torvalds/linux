/*
 * Copyright (C) 2017 SiFive
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
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
