/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 */

#include <linux/export.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/cputable.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/processor.h>
#include <asm/bug.h>
#include <asm/ptrace.h>
#include <asm/feature-fixups.h>

	.text

/*
 * This returns the high 64 bits of the product of two 64-bit numbers.
 */
_GLOBAL(mulhdu)
	cmpwi	r6,0
	cmpwi	cr1,r3,0
	mr	r10,r4
	mulhwu	r4,r4,r5
	beq	1f
	mulhwu	r0,r10,r6
	mullw	r7,r10,r5
	addc	r7,r0,r7
	addze	r4,r4
1:	beqlr	cr1		/* all done if high part of A is 0 */
	mullw	r9,r3,r5
	mulhwu	r10,r3,r5
	beq	2f
	mullw	r0,r3,r6
	mulhwu	r8,r3,r6
	addc	r7,r0,r7
	adde	r4,r4,r8
	addze	r10,r10
2:	addc	r4,r4,r9
	addze	r3,r10
	blr

/*
 * reloc_got2 runs through the .got2 section adding an offset
 * to each entry.
 */
_GLOBAL(reloc_got2)
	mflr	r11
	lis	r7,__got2_start@ha
	addi	r7,r7,__got2_start@l
	lis	r8,__got2_end@ha
	addi	r8,r8,__got2_end@l
	subf	r8,r7,r8
	srwi.	r8,r8,2
	beqlr
	mtctr	r8
	bcl	20,31,$+4
1:	mflr	r0
	lis	r4,1b@ha
	addi	r4,r4,1b@l
	subf	r0,r4,r0
	add	r7,r0,r7
2:	lwz	r0,0(r7)
	add	r0,r0,r3
	stw	r0,0(r7)
	addi	r7,r7,4
	bdnz	2b
	mtlr	r11
	blr

/*
 * call_setup_cpu - call the setup_cpu function for this cpu
 * r3 = data offset, r24 = cpu number
 *
 * Setup function is called with:
 *   r3 = data offset
 *   r4 = ptr to CPU spec (relocated)
 */
_GLOBAL(call_setup_cpu)
	addis	r4,r3,cur_cpu_spec@ha
	addi	r4,r4,cur_cpu_spec@l
	lwz	r4,0(r4)
	add	r4,r4,r3
	lwz	r5,CPU_SPEC_SETUP(r4)
	cmpwi	0,r5,0
	add	r5,r5,r3
	beqlr
	mtctr	r5
	bctr

#if defined(CONFIG_CPU_FREQ_PMAC) && defined(CONFIG_PPC_BOOK3S_32)

/* This gets called by via-pmu.c to switch the PLL selection
 * on 750fx CPU. This function should really be moved to some
 * other place (as most of the cpufreq code in via-pmu
 */
_GLOBAL(low_choose_750fx_pll)
	/* Clear MSR:EE */
	mfmsr	r7
	rlwinm	r0,r7,0,17,15
	mtmsr	r0

	/* If switching to PLL1, disable HID0:BTIC */
	cmplwi	cr0,r3,0
	beq	1f
	mfspr	r5,SPRN_HID0
	rlwinm	r5,r5,0,27,25
	sync
	mtspr	SPRN_HID0,r5
	isync
	sync

1:
	/* Calc new HID1 value */
	mfspr	r4,SPRN_HID1	/* Build a HID1:PS bit from parameter */
	rlwinm	r5,r3,16,15,15	/* Clear out HID1:PS from value read */
	rlwinm	r4,r4,0,16,14	/* Could have I used rlwimi here ? */
	or	r4,r4,r5
	mtspr	SPRN_HID1,r4

#ifdef CONFIG_SMP
	/* Store new HID1 image */
	lwz	r6,TASK_CPU(r2)
	slwi	r6,r6,2
#else
	li	r6, 0
#endif
	addis	r6,r6,nap_save_hid1@ha
	stw	r4,nap_save_hid1@l(r6)

	/* If switching to PLL0, enable HID0:BTIC */
	cmplwi	cr0,r3,0
	bne	1f
	mfspr	r5,SPRN_HID0
	ori	r5,r5,HID0_BTIC
	sync
	mtspr	SPRN_HID0,r5
	isync
	sync

1:
	/* Return */
	mtmsr	r7
	blr

_GLOBAL(low_choose_7447a_dfs)
	/* Clear MSR:EE */
	mfmsr	r7
	rlwinm	r0,r7,0,17,15
	mtmsr	r0
	
	/* Calc new HID1 value */
	mfspr	r4,SPRN_HID1
	insrwi	r4,r3,1,9	/* insert parameter into bit 9 */
	sync
	mtspr	SPRN_HID1,r4
	sync
	isync

	/* Return */
	mtmsr	r7
	blr

#endif /* CONFIG_CPU_FREQ_PMAC && CONFIG_PPC_BOOK3S_32 */

/*
 * Copy a whole page.  We use the dcbz instruction on the destination
 * to reduce memory traffic (it eliminates the unnecessary reads of
 * the destination into cache).  This requires that the destination
 * is cacheable.
 */
#define COPY_16_BYTES		\
	lwz	r6,4(r4);	\
	lwz	r7,8(r4);	\
	lwz	r8,12(r4);	\
	lwzu	r9,16(r4);	\
	stw	r6,4(r3);	\
	stw	r7,8(r3);	\
	stw	r8,12(r3);	\
	stwu	r9,16(r3)

_GLOBAL(copy_page)
	rlwinm	r5, r3, 0, L1_CACHE_BYTES - 1
	addi	r3,r3,-4

0:	twnei	r5, 0	/* WARN if r3 is not cache aligned */
	EMIT_WARN_ENTRY 0b,__FILE__,__LINE__, BUGFLAG_WARNING

	addi	r4,r4,-4

	li	r5,4

#if MAX_COPY_PREFETCH > 1
	li	r0,MAX_COPY_PREFETCH
	li	r11,4
	mtctr	r0
11:	dcbt	r11,r4
	addi	r11,r11,L1_CACHE_BYTES
	bdnz	11b
#else /* MAX_COPY_PREFETCH == 1 */
	dcbt	r5,r4
	li	r11,L1_CACHE_BYTES+4
#endif /* MAX_COPY_PREFETCH */
	li	r0,PAGE_SIZE/L1_CACHE_BYTES - MAX_COPY_PREFETCH
	crclr	4*cr0+eq
2:
	mtctr	r0
1:
	dcbt	r11,r4
	dcbz	r5,r3
	COPY_16_BYTES
#if L1_CACHE_BYTES >= 32
	COPY_16_BYTES
#if L1_CACHE_BYTES >= 64
	COPY_16_BYTES
	COPY_16_BYTES
#if L1_CACHE_BYTES >= 128
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
	COPY_16_BYTES
#endif
#endif
#endif
	bdnz	1b
	beqlr
	crnot	4*cr0+eq,4*cr0+eq
	li	r0,MAX_COPY_PREFETCH
	li	r11,4
	b	2b
EXPORT_SYMBOL(copy_page)

/*
 * Extended precision shifts.
 *
 * Updated to be valid for shift counts from 0 to 63 inclusive.
 * -- Gabriel
 *
 * R3/R4 has 64 bit value
 * R5    has shift count
 * result in R3/R4
 *
 *  ashrdi3: arithmetic right shift (sign propagation)	
 *  lshrdi3: logical right shift
 *  ashldi3: left shift
 */
_GLOBAL(__ashrdi3)
	subfic	r6,r5,32
	srw	r4,r4,r5	# LSW = count > 31 ? 0 : LSW >> count
	addi	r7,r5,32	# could be xori, or addi with -32
	slw	r6,r3,r6	# t1 = count > 31 ? 0 : MSW << (32-count)
	rlwinm	r8,r7,0,32	# t3 = (count < 32) ? 32 : 0
	sraw	r7,r3,r7	# t2 = MSW >> (count-32)
	or	r4,r4,r6	# LSW |= t1
	slw	r7,r7,r8	# t2 = (count < 32) ? 0 : t2
	sraw	r3,r3,r5	# MSW = MSW >> count
	or	r4,r4,r7	# LSW |= t2
	blr
EXPORT_SYMBOL(__ashrdi3)

_GLOBAL(__ashldi3)
	subfic	r6,r5,32
	slw	r3,r3,r5	# MSW = count > 31 ? 0 : MSW << count
	addi	r7,r5,32	# could be xori, or addi with -32
	srw	r6,r4,r6	# t1 = count > 31 ? 0 : LSW >> (32-count)
	slw	r7,r4,r7	# t2 = count < 32 ? 0 : LSW << (count-32)
	or	r3,r3,r6	# MSW |= t1
	slw	r4,r4,r5	# LSW = LSW << count
	or	r3,r3,r7	# MSW |= t2
	blr
EXPORT_SYMBOL(__ashldi3)

_GLOBAL(__lshrdi3)
	subfic	r6,r5,32
	srw	r4,r4,r5	# LSW = count > 31 ? 0 : LSW >> count
	addi	r7,r5,32	# could be xori, or addi with -32
	slw	r6,r3,r6	# t1 = count > 31 ? 0 : MSW << (32-count)
	srw	r7,r3,r7	# t2 = count < 32 ? 0 : MSW >> (count-32)
	or	r4,r4,r6	# LSW |= t1
	srw	r3,r3,r5	# MSW = MSW >> count
	or	r4,r4,r7	# LSW |= t2
	blr
EXPORT_SYMBOL(__lshrdi3)

/*
 * 64-bit comparison: __cmpdi2(s64 a, s64 b)
 * Returns 0 if a < b, 1 if a == b, 2 if a > b.
 */
_GLOBAL(__cmpdi2)
	cmpw	r3,r5
	li	r3,1
	bne	1f
	cmplw	r4,r6
	beqlr
1:	li	r3,0
	bltlr
	li	r3,2
	blr
EXPORT_SYMBOL(__cmpdi2)
/*
 * 64-bit comparison: __ucmpdi2(u64 a, u64 b)
 * Returns 0 if a < b, 1 if a == b, 2 if a > b.
 */
_GLOBAL(__ucmpdi2)
	cmplw	r3,r5
	li	r3,1
	bne	1f
	cmplw	r4,r6
	beqlr
1:	li	r3,0
	bltlr
	li	r3,2
	blr
EXPORT_SYMBOL(__ucmpdi2)

_GLOBAL(__bswapdi2)
	rotlwi  r9,r4,8
	rotlwi  r10,r3,8
	rlwimi  r9,r4,24,0,7
	rlwimi  r10,r3,24,0,7
	rlwimi  r9,r4,24,16,23
	rlwimi  r10,r3,24,16,23
	mr      r3,r9
	mr      r4,r10
	blr
EXPORT_SYMBOL(__bswapdi2)

#ifdef CONFIG_SMP
_GLOBAL(start_secondary_resume)
	/* Reset stack */
	rlwinm	r1, r1, 0, 0, 31 - THREAD_SHIFT
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_MIN_SIZE
	li	r3,0
	stw	r3,0(r1)		/* Zero the stack frame pointer	*/
	bl	start_secondary
	b	.
#endif /* CONFIG_SMP */
