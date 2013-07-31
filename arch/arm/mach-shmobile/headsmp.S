/*
 * SMP support for R-Mobile / SH-Mobile
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2010  Takashi Yoshii
 *
 * Based on vexpress, Copyright (c) 2003 ARM Limited, All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <linux/init.h>
#include <asm/memory.h>

	__CPUINIT

ENTRY(shmobile_invalidate_start)
	bl	v7_invalidate_l1
	b	secondary_startup
ENDPROC(shmobile_invalidate_start)

/*
 * Reset vector for secondary CPUs.
 * This will be mapped at address 0 by SBAR register.
 * We need _long_ jump to the physical address.
 */
	.arm
	.align  12
ENTRY(shmobile_boot_vector)
	ldr     r0, 2f
	ldr     r1, 1f
	bx	r1

ENDPROC(shmobile_boot_vector)

	.align	2
	.globl	shmobile_boot_fn
shmobile_boot_fn:
1:	.space	4
	.globl	shmobile_boot_arg
shmobile_boot_arg:
2:	.space	4

/*
 * Per-CPU SMP boot function/argument selection code based on MPIDR
 */

ENTRY(shmobile_smp_boot)
						@ r0 = MPIDR_HWID_BITMASK
	mrc	p15, 0, r1, c0, c0, 5		@ r1 = MPIDR
	and	r0, r1, r0			@ r0 = cpu_logical_map() value
	mov	r1, #0				@ r1 = CPU index
	adr	r5, 1f				@ array of per-cpu mpidr values
	adr	r6, 2f				@ array of per-cpu functions
	adr	r7, 3f				@ array of per-cpu arguments

shmobile_smp_boot_find_mpidr:
	ldr	r8, [r5, r1, lsl #2]
	cmp	r8, r0
	bne	shmobile_smp_boot_next

	ldr	r9, [r6, r1, lsl #2]
	cmp	r9, #0
	bne	shmobile_smp_boot_found

shmobile_smp_boot_next:
	add	r1, r1, #1
	cmp	r1, #CONFIG_NR_CPUS
	blo	shmobile_smp_boot_find_mpidr

	b	shmobile_smp_sleep

shmobile_smp_boot_found:
	ldr	r0, [r7, r1, lsl #2]
	mov	pc, r9
ENDPROC(shmobile_smp_boot)

ENTRY(shmobile_smp_sleep)
	wfi
	b	shmobile_smp_boot
ENDPROC(shmobile_smp_sleep)

	.globl	shmobile_smp_mpidr
shmobile_smp_mpidr:
1:	.space	CONFIG_NR_CPUS * 4
	.globl	shmobile_smp_fn
shmobile_smp_fn:
2:	.space	CONFIG_NR_CPUS * 4
	.globl	shmobile_smp_arg
shmobile_smp_arg:
3:	.space	CONFIG_NR_CPUS * 4
