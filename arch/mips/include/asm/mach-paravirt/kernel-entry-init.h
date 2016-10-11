/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2013 Cavium, Inc
 */
#ifndef __ASM_MACH_PARAVIRT_KERNEL_ENTRY_H
#define __ASM_MACH_PARAVIRT_KERNEL_ENTRY_H

#define CP0_EBASE $15, 1

	.macro  kernel_entry_setup
#ifdef CONFIG_SMP
	mfc0	t0, CP0_EBASE
	andi	t0, t0, 0x3ff		# CPUNum
	beqz	t0, 1f
	# CPUs other than zero goto smp_bootstrap
	j	smp_bootstrap
#endif /* CONFIG_SMP */

1:
	.endm

/*
 * Do SMP slave processor setup necessary before we can safely execute
 * C code.
 */
	.macro  smp_slave_setup
	mfc0	t0, CP0_EBASE
	andi	t0, t0, 0x3ff		# CPUNum
	slti	t1, t0, NR_CPUS
	bnez	t1, 1f
2:
	di
	wait
	b	2b			# Unknown CPU, loop forever.
1:
	PTR_LA	t1, paravirt_smp_sp
	PTR_SLL	t0, PTR_SCALESHIFT
	PTR_ADDU t1, t1, t0
3:
	PTR_L	sp, 0(t1)
	beqz	sp, 3b			# Spin until told to proceed.

	PTR_LA	t1, paravirt_smp_gp
	PTR_ADDU t1, t1, t0
	sync
	PTR_L	gp, 0(t1)
	.endm

#endif /* __ASM_MACH_PARAVIRT_KERNEL_ENTRY_H */
