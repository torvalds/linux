// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2012 ARM Limited
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/linkage.h>
#include <asm/unistd.h>

	.text

ENTRY(__kernel_rt_sigreturn)
	.cfi_startproc
	movi $r15, __NR_rt_sigreturn
	/*
	 * The SWID of syscall should be __NR_rt_sigreturn to synchronize
	 * the unwinding scheme in gcc
	 */
	syscall	__NR_rt_sigreturn
	.cfi_endproc
ENDPROC(__kernel_rt_sigreturn)
