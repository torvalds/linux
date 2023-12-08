/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_MACH_IP30_KERNEL_ENTRY_H
#define __ASM_MACH_IP30_KERNEL_ENTRY_H

	.macro  kernel_entry_setup
	.endm

	.macro	smp_slave_setup
	move	gp, a0
	.endm

#endif /* __ASM_MACH_IP30_KERNEL_ENTRY_H */
