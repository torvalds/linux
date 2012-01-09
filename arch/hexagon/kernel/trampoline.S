/*
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 * Trampoline sequences to be copied onto user stack.
 * This consumes a little more space than hand-assembling
 * immediate constants for use in C, but is more portable
 * to future tweaks to the Hexagon instruction set.
 */

#include <asm/unistd.h>

/*  Sig trampolines - call sys_sigreturn or sys_rt_sigreturn as appropriate */

/*  plain sigreturn is gone.  */

	.globl __rt_sigtramp_template
__rt_sigtramp_template:
	r6 = #__NR_rt_sigreturn;
	trap0(#1);
