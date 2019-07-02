/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2014 Regents of the University of California
 */

#include <linux/linkage.h>
#include <asm/unistd.h>

	.text
ENTRY(__vdso_rt_sigreturn)
	.cfi_startproc
	.cfi_signal_frame
	li a7, __NR_rt_sigreturn
	scall
	.cfi_endproc
ENDPROC(__vdso_rt_sigreturn)
