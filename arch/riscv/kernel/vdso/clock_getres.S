/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2017 SiFive
 */

#include <linux/linkage.h>
#include <asm/unistd.h>

	.text
/* int __vdso_clock_getres(clockid_t clock_id, struct timespec *res); */
ENTRY(__vdso_clock_getres)
	.cfi_startproc
	/* For now, just do the syscall. */
	li a7, __NR_clock_getres
	ecall
	ret
	.cfi_endproc
ENDPROC(__vdso_clock_getres)
