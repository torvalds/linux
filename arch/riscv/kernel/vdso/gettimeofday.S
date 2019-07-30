/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 SiFive
 */

#include <linux/linkage.h>
#include <asm/unistd.h>

	.text
/* int __vdso_gettimeofday(struct timeval *tv, struct timezone *tz); */
ENTRY(__vdso_gettimeofday)
	.cfi_startproc
	/* For now, just do the syscall. */
	li a7, __NR_gettimeofday
	ecall
	ret
	.cfi_endproc
ENDPROC(__vdso_gettimeofday)
