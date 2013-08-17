/*
 * arch/arm/common/bL_head.S -- big.LITTLE kernel re-entry point
 *
 * Created by:  Nicolas Pitre, March 2012
 * Copyright:   (C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/bL_entry.h>
#include <asm/bL_vlock.h>

#include <asm/asm-offsets.h>

.if BL_SYNC_CLUSTER_CPUS
.error "cpus must be the first member of struct bL_cluster_sync_struct"
.endif

	.macro	pr_dbg	cpu, string
#if defined(CONFIG_DEBUG_LL) && defined(DEBUG)
	b	1901f
1902:	.ascii	"CPU 0: \0CPU 1: \0CPU 2: \0CPU 3: \0"
	.ascii	"CPU 4: \0CPU 5: \0CPU 6: \0CPU 7: \0"
1903:	.asciz	"\string"
	.align
1901:	adr	r0, 1902b
	add	r0, r0, \cpu, lsl #3
	bl	printascii
	adr	r0, 1903b
	bl	printascii
#endif
	.endm

	.arm

ENTRY(bl_entry_point)

	mrc	p15, 0, r0, c0, c0, 5
	ubfx	r9, r0, #0, #4			@ r9 = cpu
	ubfx	r10, r0, #8, #4			@ r10 = cluster
	mov	r3, #BL_CPUS_PER_CLUSTER
	mla	r4, r3, r10, r9			@ r4 = canonical CPU index
	cmp	r4, #(BL_CPUS_PER_CLUSTER * BL_NR_CLUSTERS)
	blo	2f

	/* We didn't expect this CPU.  Try to make it quiet. */
1:	wfi
	wfe
	b	1b

2:	pr_dbg	r4, "kernel bl_entry_point\n"

	/*
	 * MMU is off so we need to get to bL_entry_vectors in a
	 * position independent way.
	 */
	adr	r5, 3f
	ldr	r7, 4f
	ldr	r8, 5f
	ldr	r11, 6f
	ldr	r6, [r5]
	ldr	r7, [r5, r7]			@ r7 = bL_power_up_setup_phys
	ldr	r8, [r5, r8]			@ r8 = bL_sync_phys
	ldr	r11, [r5, r11]			@ r11 = first_man_locks
	add	r5, r5, r6			@ r5 = bL_entry_vectors

bL_entry_gated:
	ldr	r6, [r5, r4, lsl #2]
	cmp	r6, #0
	/* wfeeq */
	beq	bL_entry_gated
	pr_dbg	r4, "released\n"
						@ no longer used
						@ r6 = CPU entry vector

	mov	r0, #BL_SYNC_CLUSTER_SIZE
	mla	r8, r0, r10, r8			@ r8 = bL_sync cluster base

	@ Signal that this CPU is coming UP:
	mov	r0, #CPU_COMING_UP
	strb	r0, [r8, r9]

	dsb

	@ At this point, the cluster cannot unexpectedly enter the GOING_DOWN
	@ state, because there is at least one active CPU (this CPU).

	mov     r0, #BL_VLOCK_STRUCT_SIZE
	mla     r11, r0, r10, r11               @ r11 = cluster first man lock
	mov     r0, r11
	mov     r1, r9                          @ cpu
	bl      vlock_trylock

	cmp     r0, #0                          @ failed to get the lock?
	bne     cluster_setup_wait              @ wait for cluster setup if so

	ldrb    r0, [r8, #BL_SYNC_CLUSTER_CLUSTER]
	cmp     r0, #CLUSTER_UP                 @ cluster already up?
	bne     cluster_setup                   @ if not, set up the cluster

	@ Otherwise, release the first man lock and skip setup:
	mov     r0, r11
	bl      vlock_unlock
	b       cluster_setup_complete

cluster_setup:
	@ Signal that the cluster is being brought up:
	mov	r0, #INBOUND_COMING_UP
	strb	r0, [r8, #BL_SYNC_CLUSTER_INBOUND]

	dsb

	@ Any CPU trying to take the cluster into CLUSTER_GOING_DOWN from this
	@ point onwards will observe INBOUND_COMING_UP and abort.

	@ Wait for any previously-pending cluster teardown operations to abort
	@ or complete:
cluster_teardown_wait:
	ldrb	r0, [r8, #BL_SYNC_CLUSTER_CLUSTER]
	cmp	r0, #CLUSTER_GOING_DOWN
	bne	first_man_setup
	wfe
	b	cluster_teardown_wait

first_man_setup:
	@ If the outbound gave up before teardown started, skip cluster setup:

	cmp	r0, #CLUSTER_UP
	beq     cluster_setup_complete

	@ power_up_setup is now responsible for setting up the cluster:

	cmp	r7, #0
	blxne	r7		@ Call power_up_setup if defined

	@ Leave the cluster setup critical section:

	dsb
	mov	r0, #INBOUND_NOT_COMING_UP
	strb	r0, [r8, #BL_SYNC_CLUSTER_INBOUND]
	mov	r0, #CLUSTER_UP
	strb	r0, [r8, #BL_SYNC_CLUSTER_CLUSTER]
	dsb
	sev

	mov     r0, r11
	bl      vlock_unlock
	b       cluster_setup_complete

	@ In the contended case, non-first men wait here for cluster setup
	@ to complete:
cluster_setup_wait:
	ldrb    r0, [r8, #BL_SYNC_CLUSTER_CLUSTER]
	cmp     r0, #CLUSTER_UP
	wfene
	bne     cluster_setup_wait

cluster_setup_complete:
	@ If a platform-specific CPU setup hook is needed, it should be
	@ called from here.

	@ Mark the CPU as up:

	dsb
	mov	r0, #CPU_UP
	strb	r0, [r8, r9]
	dsb
	sev

	bx	r6

3:	.word	bL_entry_vectors - .
4:	.word	bL_power_up_setup_phys - 3b
5:	.word	bL_sync_phys - 3b
6:	.word   bL_vlock_phys - 3b

ENDPROC(bl_entry_point)

	.bss

	@ Magic to size and align the first-man vlock structures
	@ so that each does not cross a 1KB boundary:

	.align	5

	.type	bL_entry_vectors, #object
ENTRY(bL_entry_vectors)
	.space	4 * BL_NR_CLUSTERS * BL_CPUS_PER_CLUSTER

	.type	bL_power_up_setup_phys, #object
ENTRY(bL_power_up_setup_phys)
	.word	0		@ set by bL_switcher_init()
