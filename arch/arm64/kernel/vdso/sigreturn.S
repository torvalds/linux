/*
 * Sigreturn trampoline for returning from a signal when the SA_RESTORER
 * flag is not set.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/linkage.h>
#include <asm/unistd.h>

	.text

	nop
ENTRY(__kernel_rt_sigreturn)
	.cfi_startproc
	.cfi_signal_frame
	.cfi_def_cfa	x29, 0
	.cfi_offset	x29, 0 * 8
	.cfi_offset	x30, 1 * 8
	mov	x8, #__NR_rt_sigreturn
	svc	#0
	.cfi_endproc
ENDPROC(__kernel_rt_sigreturn)
