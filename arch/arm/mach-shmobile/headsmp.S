/* SPDX-License-Identifier: GPL-2.0
 *
 * SMP support for R-Mobile / SH-Mobile
 *
 * Copyright (C) 2010  Magnus Damm
 * Copyright (C) 2010  Takashi Yoshii
 *
 * Based on vexpress, Copyright (c) 2003 ARM Limited, All Rights Reserved
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/threads.h>
#include <asm/assembler.h>
#include <asm/page.h>

#define SCTLR_MMU	0x01
#define BOOTROM_ADDRESS	0xE6340000
#define RWTCSRA_ADDRESS 0xE6020004
#define RWTCSRA_WOVF	0x10

/*
 * Reset vector for secondary CPUs.
 * This will be mapped at address 0 by SBAR register.
 * We need _long_ jump to the physical address.
 */
	.arm
	.align  12
ENTRY(shmobile_boot_vector)
	ldr     r1, 1f
	bx	r1

ENDPROC(shmobile_boot_vector)

	.align	2
	.globl	shmobile_boot_fn
shmobile_boot_fn:
1:	.space	4
	.globl	shmobile_boot_size
shmobile_boot_size:
	.long	. - shmobile_boot_vector

#ifdef CONFIG_ARCH_RCAR_GEN2
/*
 * Reset vector for R-Car Gen2 and RZ/G1 secondary CPUs.
 * This will be mapped at address 0 by SBAR register.
 */
ENTRY(shmobile_boot_vector_gen2)
	mrc	p15, 0, r0, c0, c0, 5		@ r0 = MPIDR
	ldr	r1, shmobile_boot_cpu_gen2
	cmp	r0, r1
	bne	shmobile_smp_continue_gen2

	mrc	p15, 0, r1, c1, c0, 0		@ r1 = SCTLR
	and	r0, r1, #SCTLR_MMU
	cmp	r0, #SCTLR_MMU
	beq	shmobile_smp_continue_gen2

	ldr	r0, rwtcsra
	mov	r1, #0
	ldrb	r1, [r0]
	and	r0, r1, #RWTCSRA_WOVF
	cmp	r0, #RWTCSRA_WOVF
	bne	shmobile_smp_continue_gen2

	ldr	r0, bootrom
	bx	r0

shmobile_smp_continue_gen2:
	ldr     r1, shmobile_boot_fn_gen2
	bx	r1

ENDPROC(shmobile_boot_vector_gen2)

	.align	4
rwtcsra:
	.word	RWTCSRA_ADDRESS
bootrom:
	.word	BOOTROM_ADDRESS
	.globl	shmobile_boot_cpu_gen2
shmobile_boot_cpu_gen2:
	.word	0x00000000

	.align	2
	.globl	shmobile_boot_fn_gen2
shmobile_boot_fn_gen2:
	.space	4
	.globl	shmobile_boot_size_gen2
shmobile_boot_size_gen2:
	.long	. - shmobile_boot_vector_gen2
#endif /* CONFIG_ARCH_RCAR_GEN2 */

/*
 * Per-CPU SMP boot function/argument selection code based on MPIDR
 */

ENTRY(shmobile_smp_boot)
	mrc	p15, 0, r1, c0, c0, 5		@ r1 = MPIDR
	and	r0, r1, #0xffffff		@ MPIDR_HWID_BITMASK
						@ r0 = cpu_logical_map() value
	mov	r1, #0				@ r1 = CPU index
	adr	r2, 1f
	ldmia	r2, {r5, r6, r7}
	add	r5, r5, r2			@ array of per-cpu mpidr values
	add	r6, r6, r2			@ array of per-cpu functions
	add	r7, r7, r2			@ array of per-cpu arguments

shmobile_smp_boot_find_mpidr:
	ldr	r8, [r5, r1, lsl #2]
	cmp	r8, r0
	bne	shmobile_smp_boot_next

	ldr	r9, [r6, r1, lsl #2]
	cmp	r9, #0
	bne	shmobile_smp_boot_found

shmobile_smp_boot_next:
	add	r1, r1, #1
	cmp	r1, #NR_CPUS
	blo	shmobile_smp_boot_find_mpidr

	b	shmobile_smp_sleep

shmobile_smp_boot_found:
	ldr	r0, [r7, r1, lsl #2]
	ret	r9
ENDPROC(shmobile_smp_boot)

ENTRY(shmobile_smp_sleep)
	wfi
	b	shmobile_smp_boot
ENDPROC(shmobile_smp_sleep)

	.align	2
1:	.long	shmobile_smp_mpidr - .
	.long	shmobile_smp_fn - 1b
	.long	shmobile_smp_arg - 1b

	.bss
	.align	2
	.globl	shmobile_smp_mpidr
shmobile_smp_mpidr:
	.space	NR_CPUS * 4
	.globl	shmobile_smp_fn
shmobile_smp_fn:
	.space	NR_CPUS * 4
	.globl	shmobile_smp_arg
shmobile_smp_arg:
	.space	NR_CPUS * 4
