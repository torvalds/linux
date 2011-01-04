/* MN10300 Low level FPU management operations
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
#include <asm/cpu-regs.h>
#include <asm/smp.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/frame.inc>

###############################################################################
#
# void fpu_disabled(void)
# - handle an exception due to the FPU being disabled
#   when CONFIG_FPU is disabled
#
###############################################################################
	.type	fpu_disabled,@function
	.globl	fpu_disabled
fpu_disabled:
	add	-4,sp
	SAVE_ALL
	mov	-1,d0
	mov	d0,(REG_ORIG_D0,fp)

	and	~EPSW_NMID,epsw

	mov	fp,d0
	call	unexpected_fpu_exception[],0
	jmp	ret_from_exception

	.size	fpu_disabled,.-fpu_disabled
