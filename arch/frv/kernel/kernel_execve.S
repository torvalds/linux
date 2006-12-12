/* in-kernel program execution
 *
 * Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/linkage.h>
#include <asm/unistd.h>

###############################################################################
#
# Do a system call from kernel instead of calling sys_execve so we end up with
# proper pt_regs.
#
# int kernel_execve(const char *filename, char *const argv[], char *const envp[])
#
# On entry: GR8/GR9/GR10: arguments to function
# On return: GR8: syscall return.
#
###############################################################################
	.globl		kernel_execve
	.type		kernel_execve,@function
kernel_execve:
	setlos		__NR_execve,gr7
	tira		gr0,#0
	bralr

	.size		kernel_execve,.-kernel_execve
