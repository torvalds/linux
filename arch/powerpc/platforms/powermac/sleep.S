/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains sleep low-level functions for PowerBook G3.
 *    Copyright (C) 1999 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *    and Paul Mackerras (paulus@samba.org).
 */

#include <asm/processor.h>
#include <asm/page.h>
#include <asm/ppc_asm.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/mmu.h>
#include <asm/feature-fixups.h>

#define MAGIC	0x4c617273	/* 'Lars' */

/*
 * Structure for storing CPU registers on the stack.
 */
#define SL_SP		0
#define SL_PC		4
#define SL_MSR		8
#define SL_SDR1		0xc
#define SL_SPRG0	0x10	/* 4 sprg's */
#define SL_DBAT0	0x20
#define SL_IBAT0	0x28
#define SL_DBAT1	0x30
#define SL_IBAT1	0x38
#define SL_DBAT2	0x40
#define SL_IBAT2	0x48
#define SL_DBAT3	0x50
#define SL_IBAT3	0x58
#define SL_DBAT4	0x60
#define SL_IBAT4	0x68
#define SL_DBAT5	0x70
#define SL_IBAT5	0x78
#define SL_DBAT6	0x80
#define SL_IBAT6	0x88
#define SL_DBAT7	0x90
#define SL_IBAT7	0x98
#define SL_TB		0xa0
#define SL_R2		0xa8
#define SL_CR		0xac
#define SL_R12		0xb0	/* r12 to r31 */
#define SL_SIZE		(SL_R12 + 80)

	.section .text
	.align	5

#if defined(CONFIG_PM) || defined(CONFIG_CPU_FREQ_PMAC) || \
    (defined(CONFIG_HOTPLUG_CPU) && defined(CONFIG_PPC32))

/* This gets called by via-pmu.c late during the sleep process.
 * The PMU was already send the sleep command and will shut us down
 * soon. We need to save all that is needed and setup the wakeup
 * vector that will be called by the ROM on wakeup
 */
_GLOBAL(low_sleep_handler)
#ifndef CONFIG_PPC_BOOK3S_32
	blr
#else
	mflr	r0
	stw	r0,4(r1)
	stwu	r1,-SL_SIZE(r1)
	mfcr	r0
	stw	r0,SL_CR(r1)
	stw	r2,SL_R2(r1)
	stmw	r12,SL_R12(r1)

	/* Save MSR & SDR1 */
	mfmsr	r4
	stw	r4,SL_MSR(r1)
	mfsdr1	r4
	stw	r4,SL_SDR1(r1)

	/* Get a stable timebase and save it */
1:	mftbu	r4
	stw	r4,SL_TB(r1)
	mftb	r5
	stw	r5,SL_TB+4(r1)
	mftbu	r3
	cmpw	r3,r4
	bne	1b

	/* Save SPRGs */
	mfsprg	r4,0
	stw	r4,SL_SPRG0(r1)
	mfsprg	r4,1
	stw	r4,SL_SPRG0+4(r1)
	mfsprg	r4,2
	stw	r4,SL_SPRG0+8(r1)
	mfsprg	r4,3
	stw	r4,SL_SPRG0+12(r1)

	/* Save BATs */
	mfdbatu	r4,0
	stw	r4,SL_DBAT0(r1)
	mfdbatl	r4,0
	stw	r4,SL_DBAT0+4(r1)
	mfdbatu	r4,1
	stw	r4,SL_DBAT1(r1)
	mfdbatl	r4,1
	stw	r4,SL_DBAT1+4(r1)
	mfdbatu	r4,2
	stw	r4,SL_DBAT2(r1)
	mfdbatl	r4,2
	stw	r4,SL_DBAT2+4(r1)
	mfdbatu	r4,3
	stw	r4,SL_DBAT3(r1)
	mfdbatl	r4,3
	stw	r4,SL_DBAT3+4(r1)
	mfibatu	r4,0
	stw	r4,SL_IBAT0(r1)
	mfibatl	r4,0
	stw	r4,SL_IBAT0+4(r1)
	mfibatu	r4,1
	stw	r4,SL_IBAT1(r1)
	mfibatl	r4,1
	stw	r4,SL_IBAT1+4(r1)
	mfibatu	r4,2
	stw	r4,SL_IBAT2(r1)
	mfibatl	r4,2
	stw	r4,SL_IBAT2+4(r1)
	mfibatu	r4,3
	stw	r4,SL_IBAT3(r1)
	mfibatl	r4,3
	stw	r4,SL_IBAT3+4(r1)

BEGIN_MMU_FTR_SECTION
	mfspr	r4,SPRN_DBAT4U
	stw	r4,SL_DBAT4(r1)
	mfspr	r4,SPRN_DBAT4L
	stw	r4,SL_DBAT4+4(r1)
	mfspr	r4,SPRN_DBAT5U
	stw	r4,SL_DBAT5(r1)
	mfspr	r4,SPRN_DBAT5L
	stw	r4,SL_DBAT5+4(r1)
	mfspr	r4,SPRN_DBAT6U
	stw	r4,SL_DBAT6(r1)
	mfspr	r4,SPRN_DBAT6L
	stw	r4,SL_DBAT6+4(r1)
	mfspr	r4,SPRN_DBAT7U
	stw	r4,SL_DBAT7(r1)
	mfspr	r4,SPRN_DBAT7L
	stw	r4,SL_DBAT7+4(r1)
	mfspr	r4,SPRN_IBAT4U
	stw	r4,SL_IBAT4(r1)
	mfspr	r4,SPRN_IBAT4L
	stw	r4,SL_IBAT4+4(r1)
	mfspr	r4,SPRN_IBAT5U
	stw	r4,SL_IBAT5(r1)
	mfspr	r4,SPRN_IBAT5L
	stw	r4,SL_IBAT5+4(r1)
	mfspr	r4,SPRN_IBAT6U
	stw	r4,SL_IBAT6(r1)
	mfspr	r4,SPRN_IBAT6L
	stw	r4,SL_IBAT6+4(r1)
	mfspr	r4,SPRN_IBAT7U
	stw	r4,SL_IBAT7(r1)
	mfspr	r4,SPRN_IBAT7L
	stw	r4,SL_IBAT7+4(r1)
END_MMU_FTR_SECTION_IFSET(MMU_FTR_USE_HIGH_BATS)

	/* Backup various CPU config stuffs */
	bl	__save_cpu_setup

	/* The ROM can wake us up via 2 different vectors:
	 *  - On wallstreet & lombard, we must write a magic
	 *    value 'Lars' at address 4 and a pointer to a
	 *    memory location containing the PC to resume from
	 *    at address 0.
	 *  - On Core99, we must store the wakeup vector at
	 *    address 0x80 and eventually it's parameters
	 *    at address 0x84. I've have some trouble with those
	 *    parameters however and I no longer use them.
	 */
	lis	r5,grackle_wake_up@ha
	addi	r5,r5,grackle_wake_up@l
	tophys(r5,r5)
	stw	r5,SL_PC(r1)
	lis	r4,KERNELBASE@h
	tophys(r5,r1)
	addi	r5,r5,SL_PC
	lis	r6,MAGIC@ha
	addi	r6,r6,MAGIC@l
	stw	r5,0(r4)
	stw	r6,4(r4)
	/* Setup stuffs at 0x80-0x84 for Core99 */
	lis	r3,core99_wake_up@ha
	addi	r3,r3,core99_wake_up@l
	tophys(r3,r3)
	stw	r3,0x80(r4)
	stw	r5,0x84(r4)
	/* Store a pointer to our backup storage into
	 * a kernel global
	 */
	lis r3,sleep_storage@ha
	addi r3,r3,sleep_storage@l
	stw r5,0(r3)

	.globl	low_cpu_die
low_cpu_die:
	/* Flush & disable all caches */
	bl	flush_disable_caches

	/* Turn off data relocation. */
	mfmsr	r3		/* Save MSR in r7 */
	rlwinm	r3,r3,0,28,26	/* Turn off DR bit */
	sync
	mtmsr	r3
	isync

BEGIN_FTR_SECTION
	/* Flush any pending L2 data prefetches to work around HW bug */
	sync
	lis	r3,0xfff0
	lwz	r0,0(r3)	/* perform cache-inhibited load to ROM */
	sync			/* (caches are disabled at this point) */
END_FTR_SECTION_IFSET(CPU_FTR_SPEC7450)

/*
 * Set the HID0 and MSR for sleep.
 */
	mfspr	r2,SPRN_HID0
	rlwinm	r2,r2,0,10,7	/* clear doze, nap */
	oris	r2,r2,HID0_SLEEP@h
	sync
	isync
	mtspr	SPRN_HID0,r2
	sync

/* This loop puts us back to sleep in case we have a spurrious
 * wakeup so that the host bridge properly stays asleep. The
 * CPU will be turned off, either after a known time (about 1
 * second) on wallstreet & lombard, or as soon as the CPU enters
 * SLEEP mode on core99
 */
	mfmsr	r2
	oris	r2,r2,MSR_POW@h
1:	sync
	mtmsr	r2
	isync
	b	1b

/*
 * Here is the resume code.
 */


/*
 * Core99 machines resume here
 * r4 has the physical address of SL_PC(sp) (unused)
 */
_GLOBAL(core99_wake_up)
	/* Make sure HID0 no longer contains any sleep bit and that data cache
	 * is disabled
	 */
	mfspr	r3,SPRN_HID0
	rlwinm	r3,r3,0,11,7		/* clear SLEEP, NAP, DOZE bits */
	rlwinm	3,r3,0,18,15		/* clear DCE, ICE */
	mtspr	SPRN_HID0,r3
	sync
	isync

	/* sanitize MSR */
	mfmsr	r3
	ori	r3,r3,MSR_EE|MSR_IP
	xori	r3,r3,MSR_EE|MSR_IP
	sync
	isync
	mtmsr	r3
	sync
	isync

	/* Recover sleep storage */
	lis	r3,sleep_storage@ha
	addi	r3,r3,sleep_storage@l
	tophys(r3,r3)
	lwz	r1,0(r3)

	/* Pass thru to older resume code ... */
/*
 * Here is the resume code for older machines.
 * r1 has the physical address of SL_PC(sp).
 */

grackle_wake_up:

	/* Restore the kernel's segment registers before
	 * we do any r1 memory access as we are not sure they
	 * are in a sane state above the first 256Mb region
	 */
	li	r0,16		/* load up segment register values */
	mtctr	r0		/* for context 0 */
	lis	r3,0x2000	/* Ku = 1, VSID = 0 */
	li	r4,0
3:	mtsrin	r3,r4
	addi	r3,r3,0x111	/* increment VSID */
	addis	r4,r4,0x1000	/* address of next segment */
	bdnz	3b
	sync
	isync

	subi	r1,r1,SL_PC

	/* Restore various CPU config stuffs */
	bl	__restore_cpu_setup

	/* Make sure all FPRs have been initialized */
	bl	reloc_offset
	bl	__init_fpu_registers

	/* Invalidate & enable L1 cache, we don't care about
	 * whatever the ROM may have tried to write to memory
	 */
	bl	__inval_enable_L1

	/* Restore the BATs, and SDR1.  Then we can turn on the MMU. */
	lwz	r4,SL_SDR1(r1)
	mtsdr1	r4
	lwz	r4,SL_SPRG0(r1)
	mtsprg	0,r4
	lwz	r4,SL_SPRG0+4(r1)
	mtsprg	1,r4
	lwz	r4,SL_SPRG0+8(r1)
	mtsprg	2,r4
	lwz	r4,SL_SPRG0+12(r1)
	mtsprg	3,r4

	lwz	r4,SL_DBAT0(r1)
	mtdbatu	0,r4
	lwz	r4,SL_DBAT0+4(r1)
	mtdbatl	0,r4
	lwz	r4,SL_DBAT1(r1)
	mtdbatu	1,r4
	lwz	r4,SL_DBAT1+4(r1)
	mtdbatl	1,r4
	lwz	r4,SL_DBAT2(r1)
	mtdbatu	2,r4
	lwz	r4,SL_DBAT2+4(r1)
	mtdbatl	2,r4
	lwz	r4,SL_DBAT3(r1)
	mtdbatu	3,r4
	lwz	r4,SL_DBAT3+4(r1)
	mtdbatl	3,r4
	lwz	r4,SL_IBAT0(r1)
	mtibatu	0,r4
	lwz	r4,SL_IBAT0+4(r1)
	mtibatl	0,r4
	lwz	r4,SL_IBAT1(r1)
	mtibatu	1,r4
	lwz	r4,SL_IBAT1+4(r1)
	mtibatl	1,r4
	lwz	r4,SL_IBAT2(r1)
	mtibatu	2,r4
	lwz	r4,SL_IBAT2+4(r1)
	mtibatl	2,r4
	lwz	r4,SL_IBAT3(r1)
	mtibatu	3,r4
	lwz	r4,SL_IBAT3+4(r1)
	mtibatl	3,r4

BEGIN_MMU_FTR_SECTION
	lwz	r4,SL_DBAT4(r1)
	mtspr	SPRN_DBAT4U,r4
	lwz	r4,SL_DBAT4+4(r1)
	mtspr	SPRN_DBAT4L,r4
	lwz	r4,SL_DBAT5(r1)
	mtspr	SPRN_DBAT5U,r4
	lwz	r4,SL_DBAT5+4(r1)
	mtspr	SPRN_DBAT5L,r4
	lwz	r4,SL_DBAT6(r1)
	mtspr	SPRN_DBAT6U,r4
	lwz	r4,SL_DBAT6+4(r1)
	mtspr	SPRN_DBAT6L,r4
	lwz	r4,SL_DBAT7(r1)
	mtspr	SPRN_DBAT7U,r4
	lwz	r4,SL_DBAT7+4(r1)
	mtspr	SPRN_DBAT7L,r4
	lwz	r4,SL_IBAT4(r1)
	mtspr	SPRN_IBAT4U,r4
	lwz	r4,SL_IBAT4+4(r1)
	mtspr	SPRN_IBAT4L,r4
	lwz	r4,SL_IBAT5(r1)
	mtspr	SPRN_IBAT5U,r4
	lwz	r4,SL_IBAT5+4(r1)
	mtspr	SPRN_IBAT5L,r4
	lwz	r4,SL_IBAT6(r1)
	mtspr	SPRN_IBAT6U,r4
	lwz	r4,SL_IBAT6+4(r1)
	mtspr	SPRN_IBAT6L,r4
	lwz	r4,SL_IBAT7(r1)
	mtspr	SPRN_IBAT7U,r4
	lwz	r4,SL_IBAT7+4(r1)
	mtspr	SPRN_IBAT7L,r4
END_MMU_FTR_SECTION_IFSET(MMU_FTR_USE_HIGH_BATS)

	/* Flush all TLBs */
	lis	r4,0x1000
1:	addic.	r4,r4,-0x1000
	tlbie	r4
	blt	1b
	sync

	/* restore the MSR and turn on the MMU */
	lwz	r3,SL_MSR(r1)
	bl	turn_on_mmu

	/* get back the stack pointer */
	tovirt(r1,r1)

	/* Restore TB */
	li	r3,0
	mttbl	r3
	lwz	r3,SL_TB(r1)
	lwz	r4,SL_TB+4(r1)
	mttbu	r3
	mttbl	r4

	/* Restore the callee-saved registers and return */
	lwz	r0,SL_CR(r1)
	mtcr	r0
	lwz	r2,SL_R2(r1)
	lmw	r12,SL_R12(r1)
	addi	r1,r1,SL_SIZE
	lwz	r0,4(r1)
	mtlr	r0
	blr

turn_on_mmu:
	mflr	r4
	tovirt(r4,r4)
	mtsrr0	r4
	mtsrr1	r3
	sync
	isync
	rfi

#endif /* defined(CONFIG_PM) || defined(CONFIG_CPU_FREQ) */

	.section .data
	.balign	L1_CACHE_BYTES
sleep_storage:
	.long 0
	.balign	L1_CACHE_BYTES, 0

#endif /* CONFIG_PPC_BOOK3S_32 */
	.section .text
