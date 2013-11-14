/*
 * arch/arm/common/mcpm_head.S -- kernel entry point for multi-cluster PM
 *
 * Created by:  Nicolas Pitre, March 2012
 * Copyright:   (C) 2012-2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Refer to Documentation/arm/cluster-pm-race-avoidance.txt
 * for details of the synchronisation algorithms used here.
 */

#include <linux/linkage.h>
#include <asm/mcpm.h>
#include <asm/assembler.h>

#include "vlock.h"

.if MCPM_SYNC_CLUSTER_CPUS
.error "cpus must be the first member of struct mcpm_sync_struct"
.endif

	.macro	pr_dbg	string
#if defined(CONFIG_DEBUG_LL) && defined(DEBUG)
	b	1901f
1902:	.asciz	"CPU"
1903:	.asciz	" cluster"
1904:	.asciz	": \string"
	.align
1901:	adr	r0, 1902b
	bl	printascii
	mov	r0, r9
	bl	printhex2
	adr	r0, 1903b
	bl	printascii
	mov	r0, r10
	bl	printhex2
	adr	r0, 1904b
	bl	printascii
#endif
	.endm

	.arm
	.align

ENTRY(mcpm_entry_point)

 ARM_BE8(setend        be)
 THUMB(	adr	r12, BSYM(1f)	)
 THUMB(	bx	r12		)
 THUMB(	.thumb			)
1:
	mrc	p15, 0, r0, c0, c0, 5		@ MPIDR
	ubfx	r9, r0, #0, #8			@ r9 = cpu
	ubfx	r10, r0, #8, #8			@ r10 = cluster
	mov	r3, #MAX_CPUS_PER_CLUSTER
	mla	r4, r3, r10, r9			@ r4 = canonical CPU index
	cmp	r4, #(MAX_CPUS_PER_CLUSTER * MAX_NR_CLUSTERS)
	blo	2f

	/* We didn't expect this CPU.  Try to cheaply make it quiet. */
1:	wfi
	wfe
	b	1b

2:	pr_dbg	"kernel mcpm_entry_point\n"

	/*
	 * MMU is off so we need to get to various variables in a
	 * position independent way.
	 */
	adr	r5, 3f
	ldmia	r5, {r0, r6, r7, r8, r11}
	add	r0, r5, r0			@ r0 = mcpm_entry_early_pokes
	add	r6, r5, r6			@ r6 = mcpm_entry_vectors
	ldr	r7, [r5, r7]			@ r7 = mcpm_power_up_setup_phys
	add	r8, r5, r8			@ r8 = mcpm_sync
	add	r11, r5, r11			@ r11 = first_man_locks

	@ Perform an early poke, if any
	add	r0, r0, r4, lsl #3
	ldmia	r0, {r0, r1}
	teq	r0, #0
	strne	r1, [r0]

	mov	r0, #MCPM_SYNC_CLUSTER_SIZE
	mla	r8, r0, r10, r8			@ r8 = sync cluster base

	@ Signal that this CPU is coming UP:
	mov	r0, #CPU_COMING_UP
	mov	r5, #MCPM_SYNC_CPU_SIZE
	mla	r5, r9, r5, r8			@ r5 = sync cpu address
	strb	r0, [r5]

	@ At this point, the cluster cannot unexpectedly enter the GOING_DOWN
	@ state, because there is at least one active CPU (this CPU).

	mov	r0, #VLOCK_SIZE
	mla	r11, r0, r10, r11		@ r11 = cluster first man lock
	mov	r0, r11
	mov	r1, r9				@ cpu
	bl	vlock_trylock			@ implies DMB

	cmp	r0, #0				@ failed to get the lock?
	bne	mcpm_setup_wait		@ wait for cluster setup if so

	ldrb	r0, [r8, #MCPM_SYNC_CLUSTER_CLUSTER]
	cmp	r0, #CLUSTER_UP			@ cluster already up?
	bne	mcpm_setup			@ if not, set up the cluster

	@ Otherwise, release the first man lock and skip setup:
	mov	r0, r11
	bl	vlock_unlock
	b	mcpm_setup_complete

mcpm_setup:
	@ Control dependency implies strb not observable before previous ldrb.

	@ Signal that the cluster is being brought up:
	mov	r0, #INBOUND_COMING_UP
	strb	r0, [r8, #MCPM_SYNC_CLUSTER_INBOUND]
	dmb

	@ Any CPU trying to take the cluster into CLUSTER_GOING_DOWN from this
	@ point onwards will observe INBOUND_COMING_UP and abort.

	@ Wait for any previously-pending cluster teardown operations to abort
	@ or complete:
mcpm_teardown_wait:
	ldrb	r0, [r8, #MCPM_SYNC_CLUSTER_CLUSTER]
	cmp	r0, #CLUSTER_GOING_DOWN
	bne	first_man_setup
	wfe
	b	mcpm_teardown_wait

first_man_setup:
	dmb

	@ If the outbound gave up before teardown started, skip cluster setup:

	cmp	r0, #CLUSTER_UP
	beq	mcpm_setup_leave

	@ power_up_setup is now responsible for setting up the cluster:

	cmp	r7, #0
	mov	r0, #1		@ second (cluster) affinity level
	blxne	r7		@ Call power_up_setup if defined
	dmb

	mov	r0, #CLUSTER_UP
	strb	r0, [r8, #MCPM_SYNC_CLUSTER_CLUSTER]
	dmb

mcpm_setup_leave:
	@ Leave the cluster setup critical section:

	mov	r0, #INBOUND_NOT_COMING_UP
	strb	r0, [r8, #MCPM_SYNC_CLUSTER_INBOUND]
	dsb	st
	sev

	mov	r0, r11
	bl	vlock_unlock	@ implies DMB
	b	mcpm_setup_complete

	@ In the contended case, non-first men wait here for cluster setup
	@ to complete:
mcpm_setup_wait:
	ldrb	r0, [r8, #MCPM_SYNC_CLUSTER_CLUSTER]
	cmp	r0, #CLUSTER_UP
	wfene
	bne	mcpm_setup_wait
	dmb

mcpm_setup_complete:
	@ If a platform-specific CPU setup hook is needed, it is
	@ called from here.

	cmp	r7, #0
	mov	r0, #0		@ first (CPU) affinity level
	blxne	r7		@ Call power_up_setup if defined
	dmb

	@ Mark the CPU as up:

	mov	r0, #CPU_UP
	strb	r0, [r5]

	@ Observability order of CPU_UP and opening of the gate does not matter.

mcpm_entry_gated:
	ldr	r5, [r6, r4, lsl #2]		@ r5 = CPU entry vector
	cmp	r5, #0
	wfeeq
	beq	mcpm_entry_gated
	dmb

	pr_dbg	"released\n"
	bx	r5

	.align	2

3:	.word	mcpm_entry_early_pokes - .
	.word	mcpm_entry_vectors - 3b
	.word	mcpm_power_up_setup_phys - 3b
	.word	mcpm_sync - 3b
	.word	first_man_locks - 3b

ENDPROC(mcpm_entry_point)

	.bss

	.align	CACHE_WRITEBACK_ORDER
	.type	first_man_locks, #object
first_man_locks:
	.space	VLOCK_SIZE * MAX_NR_CLUSTERS
	.align	CACHE_WRITEBACK_ORDER

	.type	mcpm_entry_vectors, #object
ENTRY(mcpm_entry_vectors)
	.space	4 * MAX_NR_CLUSTERS * MAX_CPUS_PER_CLUSTER

	.type	mcpm_entry_early_pokes, #object
ENTRY(mcpm_entry_early_pokes)
	.space	8 * MAX_NR_CLUSTERS * MAX_CPUS_PER_CLUSTER

	.type	mcpm_power_up_setup_phys, #object
ENTRY(mcpm_power_up_setup_phys)
	.space  4		@ set by mcpm_sync_init()
