/*
 * Copyright (C) 2014 Regents of the University of California
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
ENTRY(__vdso_rt_sigreturn)
	.cfi_startproc
	.cfi_signal_frame
	li a7, __NR_rt_sigreturn
	scall
	.cfi_endproc
ENDPROC(__vdso_rt_sigreturn)
