/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Chris Dearman (chris@mips.com)
 * Copyright (C) 2007 Mips Technologies, Inc.
 */
#ifndef __ASM_MACH_MIPS_KERNEL_ENTRY_INIT_H
#define __ASM_MACH_MIPS_KERNEL_ENTRY_INIT_H

	.macro	kernel_entry_setup
	.endm

/*
 * Do SMP slave processor setup necessary before we can safely execute C code.
 */
	.macro	smp_slave_setup
	.endm

#endif /* __ASM_MACH_MIPS_KERNEL_ENTRY_INIT_H */
