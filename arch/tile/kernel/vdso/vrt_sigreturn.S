/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/linkage.h>
#include <arch/abi.h>
#include <asm/unistd.h>

/*
 * Note that libc has a copy of this function that it uses to compare
 * against the PC when a stack backtrace ends, so if this code is
 * changed, the libc implementation(s) should also be updated.
 */
ENTRY(__vdso_rt_sigreturn)
	moveli TREG_SYSCALL_NR_NAME, __NR_rt_sigreturn
	swint1
	/* We don't use ENDPROC to avoid tagging this symbol as FUNC,
	 * which confuses the perf tool.
	 */
	END(__vdso_rt_sigreturn)
