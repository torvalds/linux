/* MN10300 In-kernel program execution
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/linkage.h>
#include <asm/unistd.h>

###############################################################################
#
# Do a system call from kernel instead of calling sys_execve so we end up with
# proper pt_regs.
#
# int kernel_execve(const char *filename, char *const argv[],
#		    char *const envp[])
#
# On entry: D0/D1/8(SP): arguments to function
# On return: D0: syscall return.
#
###############################################################################
	.globl		kernel_execve
	.type		kernel_execve,@function
kernel_execve:
	mov		a3,a1
	mov		d0,a0
	mov		(12,sp),a3
	mov		+__NR_execve,d0
	syscall		0
	mov		a1,a3
	rets

	.size		kernel_execve,.-kernel_execve
