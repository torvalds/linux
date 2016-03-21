/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * kexec bits:
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 * PPC44x port. Copyright (C) 2011,  IBM Corporation
 * 		Author: Suzuki Poulose <suzuki@in.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

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
#include <asm/kexec.h>
#include <asm/bug.h>
#include <asm/ptrace.h>

	.text

/*
 * We store the saved ksp_limit in the unused part
 * of the STACK_FRAME_OVERHEAD
 */
_GLOBAL(call_do_softirq)
	mflr	r0
	stw	r0,4(r1)
	lwz	r10,THREAD+KSP_LIMIT(r2)
	addi	r11,r3,THREAD_INFO_GAP
	stwu	r1,THREAD_SIZE-STACK_FRAME_OVERHEAD(r3)
	mr	r1,r3
	stw	r10,8(r1)
	stw	r11,THREAD+KSP_LIMIT(r2)
	bl	__do_softirq
	lwz	r10,8(r1)
	lwz	r1,0(r1)
	lwz	r0,4(r1)
	stw	r10,THREAD+KSP_LIMIT(r2)
	mtlr	r0
	blr

/*
 * void call_do_irq(struct pt_regs *regs, struct thread_info *irqtp);
 */
_GLOBAL(call_do_irq)
	mflr	r0
	stw	r0,4(r1)
	lwz	r10,THREAD+KSP_LIMIT(r2)
	addi	r11,r4,THREAD_INFO_GAP
	stwu	r1,THREAD_SIZE-STACK_FRAME_OVERHEAD(r4)
	mr	r1,r4
	stw	r10,8(r1)
	stw	r11,THREAD+KSP_LIMIT(r2)
	bl	__do_irq
	lwz	r10,8(r1)
	lwz	r1,0(r1)
	lwz	r0,4(r1)
	stw	r10,THREAD+KSP_LIMIT(r2)
	mtlr	r0
	blr

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
 * sub_reloc_offset(x) returns x - reloc_offset().
 */
_GLOBAL(sub_reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r5
	lis	r4,1b@ha
	addi	r4,r4,1b@l
	subf	r5,r4,r5
	subf	r3,r5,r3
	mtlr	r0
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
	bl	1f
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

#if defined(CONFIG_CPU_FREQ_PMAC) && defined(CONFIG_6xx)

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

	/* Store new HID1 image */
	CURRENT_THREAD_INFO(r6, r1)
	lwz	r6,TI_CPU(r6)
	slwi	r6,r6,2
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

#endif /* CONFIG_CPU_FREQ_PMAC && CONFIG_6xx */

/*
 * complement mask on the msr then "or" some values on.
 *     _nmask_and_or_msr(nmask, value_to_or)
 */
_GLOBAL(_nmask_and_or_msr)
	mfmsr	r0		/* Get current msr */
	andc	r0,r0,r3	/* And off the bits set in r3 (first parm) */
	or	r0,r0,r4	/* Or on the bits in r4 (second parm) */
	SYNC			/* Some chip revs have problems here... */
	mtmsr	r0		/* Update machine state */
	isync
	blr			/* Done */

#ifdef CONFIG_40x

/*
 * Do an IO access in real mode
 */
_GLOBAL(real_readb)
	mfmsr	r7
	ori	r0,r7,MSR_DR
	xori	r0,r0,MSR_DR
	sync
	mtmsr	r0
	sync
	isync
	lbz	r3,0(r3)
	sync
	mtmsr	r7
	sync
	isync
	blr

	/*
 * Do an IO access in real mode
 */
_GLOBAL(real_writeb)
	mfmsr	r7
	ori	r0,r7,MSR_DR
	xori	r0,r0,MSR_DR
	sync
	mtmsr	r0
	sync
	isync
	stb	r3,0(r4)
	sync
	mtmsr	r7
	sync
	isync
	blr

#endif /* CONFIG_40x */


/*
 * Flush instruction cache.
 * This is a no-op on the 601.
 */
#ifndef CONFIG_PPC_8xx
_GLOBAL(flush_instruction_cache)
#if defined(CONFIG_4xx)
#ifdef CONFIG_403GCX
	li      r3, 512
	mtctr   r3
	lis     r4, KERNELBASE@h
1:	iccci   0, r4
	addi    r4, r4, 16
	bdnz    1b
#else
	lis	r3, KERNELBASE@h
	iccci	0,r3
#endif
#elif CONFIG_FSL_BOOKE
BEGIN_FTR_SECTION
	mfspr   r3,SPRN_L1CSR0
	ori     r3,r3,L1CSR0_CFI|L1CSR0_CLFC
	/* msync; isync recommended here */
	mtspr   SPRN_L1CSR0,r3
	isync
	blr
END_FTR_SECTION_IFSET(CPU_FTR_UNIFIED_ID_CACHE)
	mfspr	r3,SPRN_L1CSR1
	ori	r3,r3,L1CSR1_ICFI|L1CSR1_ICLFR
	mtspr	SPRN_L1CSR1,r3
#else
	mfspr	r3,SPRN_PVR
	rlwinm	r3,r3,16,16,31
	cmpwi	0,r3,1
	beqlr			/* for 601, do nothing */
	/* 603/604 processor - use invalidate-all bit in HID0 */
	mfspr	r3,SPRN_HID0
	ori	r3,r3,HID0_ICFI
	mtspr	SPRN_HID0,r3
#endif /* CONFIG_4xx */
	isync
	blr
#endif /* CONFIG_PPC_8xx */

/*
 * Write any modified data cache blocks out to memory
 * and invalidate the corresponding instruction cache blocks.
 * This is a no-op on the 601.
 *
 * flush_icache_range(unsigned long start, unsigned long stop)
 */
_KPROBE(flush_icache_range)
BEGIN_FTR_SECTION
	PURGE_PREFETCHED_INS
	blr				/* for 601, do nothing */
END_FTR_SECTION_IFSET(CPU_FTR_COHERENT_ICACHE)
	rlwinm	r3,r3,0,0,31 - L1_CACHE_SHIFT
	subf	r4,r3,r4
	addi	r4,r4,L1_CACHE_BYTES - 1
	srwi.	r4,r4,L1_CACHE_SHIFT
	beqlr
	mtctr	r4
	mr	r6,r3
1:	dcbst	0,r3
	addi	r3,r3,L1_CACHE_BYTES
	bdnz	1b
	sync				/* wait for dcbst's to get to ram */
#ifndef CONFIG_44x
	mtctr	r4
2:	icbi	0,r6
	addi	r6,r6,L1_CACHE_BYTES
	bdnz	2b
#else
	/* Flash invalidate on 44x because we are passed kmapped addresses and
	   this doesn't work for userspace pages due to the virtually tagged
	   icache.  Sigh. */
	iccci	0, r0
#endif
	sync				/* additional sync needed on g4 */
	isync
	blr
/*
 * Flush a particular page from the data cache to RAM.
 * Note: this is necessary because the instruction cache does *not*
 * snoop from the data cache.
 * This is a no-op on the 601 which has a unified cache.
 *
 *	void __flush_dcache_icache(void *page)
 */
_GLOBAL(__flush_dcache_icache)
BEGIN_FTR_SECTION
	PURGE_PREFETCHED_INS
	blr
END_FTR_SECTION_IFSET(CPU_FTR_COHERENT_ICACHE)
	rlwinm	r3,r3,0,0,31-PAGE_SHIFT		/* Get page base address */
	li	r4,PAGE_SIZE/L1_CACHE_BYTES	/* Number of lines in a page */
	mtctr	r4
	mr	r6,r3
0:	dcbst	0,r3				/* Write line to ram */
	addi	r3,r3,L1_CACHE_BYTES
	bdnz	0b
	sync
#ifdef CONFIG_44x
	/* We don't flush the icache on 44x. Those have a virtual icache
	 * and we don't have access to the virtual address here (it's
	 * not the page vaddr but where it's mapped in user space). The
	 * flushing of the icache on these is handled elsewhere, when
	 * a change in the address space occurs, before returning to
	 * user space
	 */
BEGIN_MMU_FTR_SECTION
	blr
END_MMU_FTR_SECTION_IFSET(MMU_FTR_TYPE_44x)
#endif /* CONFIG_44x */
	mtctr	r4
1:	icbi	0,r6
	addi	r6,r6,L1_CACHE_BYTES
	bdnz	1b
	sync
	isync
	blr

#ifndef CONFIG_BOOKE
/*
 * Flush a particular page from the data cache to RAM, identified
 * by its physical address.  We turn off the MMU so we can just use
 * the physical address (this may be a highmem page without a kernel
 * mapping).
 *
 *	void __flush_dcache_icache_phys(unsigned long physaddr)
 */
_GLOBAL(__flush_dcache_icache_phys)
BEGIN_FTR_SECTION
	PURGE_PREFETCHED_INS
	blr					/* for 601, do nothing */
END_FTR_SECTION_IFSET(CPU_FTR_COHERENT_ICACHE)
	mfmsr	r10
	rlwinm	r0,r10,0,28,26			/* clear DR */
	mtmsr	r0
	isync
	rlwinm	r3,r3,0,0,31-PAGE_SHIFT		/* Get page base address */
	li	r4,PAGE_SIZE/L1_CACHE_BYTES	/* Number of lines in a page */
	mtctr	r4
	mr	r6,r3
0:	dcbst	0,r3				/* Write line to ram */
	addi	r3,r3,L1_CACHE_BYTES
	bdnz	0b
	sync
	mtctr	r4
1:	icbi	0,r6
	addi	r6,r6,L1_CACHE_BYTES
	bdnz	1b
	sync
	mtmsr	r10				/* restore DR */
	isync
	blr
#endif /* CONFIG_BOOKE */

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
	addi	r3,r3,-4
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

_GLOBAL(abs)
	srawi	r4,r3,31
	xor	r3,r3,r4
	sub	r3,r3,r4
	blr

#ifdef CONFIG_SMP
_GLOBAL(start_secondary_resume)
	/* Reset stack */
	CURRENT_THREAD_INFO(r1, r1)
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	li	r3,0
	stw	r3,0(r1)		/* Zero the stack frame pointer	*/
	bl	start_secondary
	b	.
#endif /* CONFIG_SMP */
	
/*
 * This routine is just here to keep GCC happy - sigh...
 */
_GLOBAL(__main)
	blr

#ifdef CONFIG_KEXEC
	/*
	 * Must be relocatable PIC code callable as a C function.
	 */
	.globl relocate_new_kernel
relocate_new_kernel:
	/* r3 = page_list   */
	/* r4 = reboot_code_buffer */
	/* r5 = start_address      */

#ifdef CONFIG_FSL_BOOKE

	mr	r29, r3
	mr	r30, r4
	mr	r31, r5

#define ENTRY_MAPPING_KEXEC_SETUP
#include "fsl_booke_entry_mapping.S"
#undef ENTRY_MAPPING_KEXEC_SETUP

	mr      r3, r29
	mr      r4, r30
	mr      r5, r31

	li	r0, 0
#elif defined(CONFIG_44x)

	/* Save our parameters */
	mr	r29, r3
	mr	r30, r4
	mr	r31, r5

#ifdef CONFIG_PPC_47x
	/* Check for 47x cores */
	mfspr	r3,SPRN_PVR
	srwi	r3,r3,16
	cmplwi	cr0,r3,PVR_476FPE@h
	beq	setup_map_47x
	cmplwi	cr0,r3,PVR_476@h
	beq	setup_map_47x
	cmplwi	cr0,r3,PVR_476_ISS@h
	beq	setup_map_47x
#endif /* CONFIG_PPC_47x */
	
/*
 * Code for setting up 1:1 mapping for PPC440x for KEXEC
 *
 * We cannot switch off the MMU on PPC44x.
 * So we:
 * 1) Invalidate all the mappings except the one we are running from.
 * 2) Create a tmp mapping for our code in the other address space(TS) and
 *    jump to it. Invalidate the entry we started in.
 * 3) Create a 1:1 mapping for 0-2GiB in chunks of 256M in original TS.
 * 4) Jump to the 1:1 mapping in original TS.
 * 5) Invalidate the tmp mapping.
 *
 * - Based on the kexec support code for FSL BookE
 *
 */

	/* 
	 * Load the PID with kernel PID (0).
	 * Also load our MSR_IS and TID to MMUCR for TLB search.
	 */
	li	r3, 0
	mtspr	SPRN_PID, r3
	mfmsr	r4
	andi.	r4,r4,MSR_IS@l
	beq	wmmucr
	oris	r3,r3,PPC44x_MMUCR_STS@h
wmmucr:
	mtspr	SPRN_MMUCR,r3
	sync

	/*
	 * Invalidate all the TLB entries except the current entry
	 * where we are running from
	 */
	bl	0f				/* Find our address */
0:	mflr	r5				/* Make it accessible */
	tlbsx	r23,0,r5			/* Find entry we are in */
	li	r4,0				/* Start at TLB entry 0 */
	li	r3,0				/* Set PAGEID inval value */
1:	cmpw	r23,r4				/* Is this our entry? */
	beq	skip				/* If so, skip the inval */
	tlbwe	r3,r4,PPC44x_TLB_PAGEID		/* If not, inval the entry */
skip:
	addi	r4,r4,1				/* Increment */
	cmpwi	r4,64				/* Are we done?	*/
	bne	1b				/* If not, repeat */
	isync

	/* Create a temp mapping and jump to it */
	andi.	r6, r23, 1		/* Find the index to use */
	addi	r24, r6, 1		/* r24 will contain 1 or 2 */

	mfmsr	r9			/* get the MSR */
	rlwinm	r5, r9, 27, 31, 31	/* Extract the MSR[IS] */
	xori	r7, r5, 1		/* Use the other address space */

	/* Read the current mapping entries */
	tlbre	r3, r23, PPC44x_TLB_PAGEID
	tlbre	r4, r23, PPC44x_TLB_XLAT
	tlbre	r5, r23, PPC44x_TLB_ATTRIB

	/* Save our current XLAT entry */
	mr	r25, r4

	/* Extract the TLB PageSize */
	li	r10, 1 			/* r10 will hold PageSize */
	rlwinm	r11, r3, 0, 24, 27	/* bits 24-27 */

	/* XXX: As of now we use 256M, 4K pages */
	cmpwi	r11, PPC44x_TLB_256M
	bne	tlb_4k
	rotlwi	r10, r10, 28		/* r10 = 256M */
	b	write_out
tlb_4k:
	cmpwi	r11, PPC44x_TLB_4K
	bne	default
	rotlwi	r10, r10, 12		/* r10 = 4K */
	b	write_out
default:
	rotlwi	r10, r10, 10		/* r10 = 1K */

write_out:
	/*
	 * Write out the tmp 1:1 mapping for this code in other address space
	 * Fixup  EPN = RPN , TS=other address space
	 */
	insrwi	r3, r7, 1, 23		/* Bit 23 is TS for PAGEID field */

	/* Write out the tmp mapping entries */
	tlbwe	r3, r24, PPC44x_TLB_PAGEID
	tlbwe	r4, r24, PPC44x_TLB_XLAT
	tlbwe	r5, r24, PPC44x_TLB_ATTRIB

	subi	r11, r10, 1		/* PageOffset Mask = PageSize - 1 */
	not	r10, r11		/* Mask for PageNum */

	/* Switch to other address space in MSR */
	insrwi	r9, r7, 1, 26		/* Set MSR[IS] = r7 */

	bl	1f
1:	mflr	r8
	addi	r8, r8, (2f-1b)		/* Find the target offset */

	/* Jump to the tmp mapping */
	mtspr	SPRN_SRR0, r8
	mtspr	SPRN_SRR1, r9
	rfi

2:
	/* Invalidate the entry we were executing from */
	li	r3, 0
	tlbwe	r3, r23, PPC44x_TLB_PAGEID

	/* attribute fields. rwx for SUPERVISOR mode */
	li	r5, 0
	ori	r5, r5, (PPC44x_TLB_SW | PPC44x_TLB_SR | PPC44x_TLB_SX | PPC44x_TLB_G)

	/* Create 1:1 mapping in 256M pages */
	xori	r7, r7, 1			/* Revert back to Original TS */

	li	r8, 0				/* PageNumber */
	li	r6, 3				/* TLB Index, start at 3  */

next_tlb:
	rotlwi	r3, r8, 28			/* Create EPN (bits 0-3) */
	mr	r4, r3				/* RPN = EPN  */
	ori	r3, r3, (PPC44x_TLB_VALID | PPC44x_TLB_256M) /* SIZE = 256M, Valid */
	insrwi	r3, r7, 1, 23			/* Set TS from r7 */

	tlbwe	r3, r6, PPC44x_TLB_PAGEID	/* PageID field : EPN, V, SIZE */
	tlbwe	r4, r6, PPC44x_TLB_XLAT		/* Address translation : RPN   */
	tlbwe	r5, r6, PPC44x_TLB_ATTRIB	/* Attributes */

	addi	r8, r8, 1			/* Increment PN */
	addi	r6, r6, 1			/* Increment TLB Index */
	cmpwi	r8, 8				/* Are we done ? */
	bne	next_tlb
	isync

	/* Jump to the new mapping 1:1 */
	li	r9,0
	insrwi	r9, r7, 1, 26			/* Set MSR[IS] = r7 */

	bl	1f
1:	mflr	r8
	and	r8, r8, r11			/* Get our offset within page */
	addi	r8, r8, (2f-1b)

	and	r5, r25, r10			/* Get our target PageNum */
	or	r8, r8, r5			/* Target jump address */

	mtspr	SPRN_SRR0, r8
	mtspr	SPRN_SRR1, r9
	rfi
2:
	/* Invalidate the tmp entry we used */
	li	r3, 0
	tlbwe	r3, r24, PPC44x_TLB_PAGEID
	sync
	b	ppc44x_map_done

#ifdef CONFIG_PPC_47x

	/* 1:1 mapping for 47x */

setup_map_47x:

	/*
	 * Load the kernel pid (0) to PID and also to MMUCR[TID].
	 * Also set the MSR IS->MMUCR STS
	 */
	li	r3, 0
	mtspr	SPRN_PID, r3			/* Set PID */
	mfmsr	r4				/* Get MSR */
	andi.	r4, r4, MSR_IS@l		/* TS=1? */
	beq	1f				/* If not, leave STS=0 */
	oris	r3, r3, PPC47x_MMUCR_STS@h	/* Set STS=1 */
1:	mtspr	SPRN_MMUCR, r3			/* Put MMUCR */
	sync

	/* Find the entry we are running from */
	bl	2f
2:	mflr	r23
	tlbsx	r23, 0, r23
	tlbre	r24, r23, 0			/* TLB Word 0 */
	tlbre	r25, r23, 1			/* TLB Word 1 */
	tlbre	r26, r23, 2			/* TLB Word 2 */


	/*
	 * Invalidates all the tlb entries by writing to 256 RPNs(r4)
	 * of 4k page size in all  4 ways (0-3 in r3).
	 * This would invalidate the entire UTLB including the one we are
	 * running from. However the shadow TLB entries would help us 
	 * to continue the execution, until we flush them (rfi/isync).
	 */
	addis	r3, 0, 0x8000			/* specify the way */
	addi	r4, 0, 0			/* TLB Word0 = (EPN=0, VALID = 0) */
	addi	r5, 0, 0
	b	clear_utlb_entry

	/* Align the loop to speed things up. from head_44x.S */
	.align	6

clear_utlb_entry:

	tlbwe	r4, r3, 0
	tlbwe	r5, r3, 1
	tlbwe	r5, r3, 2
	addis	r3, r3, 0x2000			/* Increment the way */
	cmpwi	r3, 0
	bne	clear_utlb_entry
	addis	r3, 0, 0x8000
	addis	r4, r4, 0x100			/* Increment the EPN */
	cmpwi	r4, 0
	bne	clear_utlb_entry

	/* Create the entries in the other address space */
	mfmsr	r5
	rlwinm	r7, r5, 27, 31, 31		/* Get the TS (Bit 26) from MSR */
	xori	r7, r7, 1			/* r7 = !TS */

	insrwi	r24, r7, 1, 21			/* Change the TS in the saved TLB word 0 */

	/* 
	 * write out the TLB entries for the tmp mapping
	 * Use way '0' so that we could easily invalidate it later.
	 */
	lis	r3, 0x8000			/* Way '0' */ 

	tlbwe	r24, r3, 0
	tlbwe	r25, r3, 1
	tlbwe	r26, r3, 2

	/* Update the msr to the new TS */
	insrwi	r5, r7, 1, 26

	bl	1f
1:	mflr	r6
	addi	r6, r6, (2f-1b)

	mtspr	SPRN_SRR0, r6
	mtspr	SPRN_SRR1, r5
	rfi

	/* 
	 * Now we are in the tmp address space.
	 * Create a 1:1 mapping for 0-2GiB in the original TS.
	 */
2:
	li	r3, 0
	li	r4, 0				/* TLB Word 0 */
	li	r5, 0				/* TLB Word 1 */
	li	r6, 0
	ori	r6, r6, PPC47x_TLB2_S_RWX	/* TLB word 2 */

	li	r8, 0				/* PageIndex */

	xori	r7, r7, 1			/* revert back to original TS */

write_utlb:
	rotlwi	r5, r8, 28			/* RPN = PageIndex * 256M */
						/* ERPN = 0 as we don't use memory above 2G */

	mr	r4, r5				/* EPN = RPN */
	ori	r4, r4, (PPC47x_TLB0_VALID | PPC47x_TLB0_256M)
	insrwi	r4, r7, 1, 21			/* Insert the TS to Word 0 */

	tlbwe	r4, r3, 0			/* Write out the entries */
	tlbwe	r5, r3, 1
	tlbwe	r6, r3, 2
	addi	r8, r8, 1
	cmpwi	r8, 8				/* Have we completed ? */
	bne	write_utlb

	/* make sure we complete the TLB write up */
	isync

	/* 
	 * Prepare to jump to the 1:1 mapping.
	 * 1) Extract page size of the tmp mapping
	 *    DSIZ = TLB_Word0[22:27]
	 * 2) Calculate the physical address of the address
	 *    to jump to.
	 */
	rlwinm	r10, r24, 0, 22, 27

	cmpwi	r10, PPC47x_TLB0_4K
	bne	0f
	li	r10, 0x1000			/* r10 = 4k */
	bl	1f

0:
	/* Defaults to 256M */
	lis	r10, 0x1000
	
	bl	1f
1:	mflr	r4
	addi	r4, r4, (2f-1b)			/* virtual address  of 2f */

	subi	r11, r10, 1			/* offsetmask = Pagesize - 1 */
	not	r10, r11			/* Pagemask = ~(offsetmask) */

	and	r5, r25, r10			/* Physical page */
	and	r6, r4, r11			/* offset within the current page */

	or	r5, r5, r6			/* Physical address for 2f */

	/* Switch the TS in MSR to the original one */
	mfmsr	r8
	insrwi	r8, r7, 1, 26

	mtspr	SPRN_SRR1, r8
	mtspr	SPRN_SRR0, r5
	rfi

2:
	/* Invalidate the tmp mapping */
	lis	r3, 0x8000			/* Way '0' */

	clrrwi	r24, r24, 12			/* Clear the valid bit */
	tlbwe	r24, r3, 0
	tlbwe	r25, r3, 1
	tlbwe	r26, r3, 2

	/* Make sure we complete the TLB write and flush the shadow TLB */
	isync

#endif

ppc44x_map_done:


	/* Restore the parameters */
	mr	r3, r29
	mr	r4, r30
	mr	r5, r31

	li	r0, 0
#else
	li	r0, 0

	/*
	 * Set Machine Status Register to a known status,
	 * switch the MMU off and jump to 1: in a single step.
	 */

	mr	r8, r0
	ori     r8, r8, MSR_RI|MSR_ME
	mtspr	SPRN_SRR1, r8
	addi	r8, r4, 1f - relocate_new_kernel
	mtspr	SPRN_SRR0, r8
	sync
	rfi

1:
#endif
	/* from this point address translation is turned off */
	/* and interrupts are disabled */

	/* set a new stack at the bottom of our page... */
	/* (not really needed now) */
	addi	r1, r4, KEXEC_CONTROL_PAGE_SIZE - 8 /* for LR Save+Back Chain */
	stw	r0, 0(r1)

	/* Do the copies */
	li	r6, 0 /* checksum */
	mr	r0, r3
	b	1f

0:	/* top, read another word for the indirection page */
	lwzu	r0, 4(r3)

1:
	/* is it a destination page? (r8) */
	rlwinm.	r7, r0, 0, 31, 31 /* IND_DESTINATION (1<<0) */
	beq	2f

	rlwinm	r8, r0, 0, 0, 19 /* clear kexec flags, page align */
	b	0b

2:	/* is it an indirection page? (r3) */
	rlwinm.	r7, r0, 0, 30, 30 /* IND_INDIRECTION (1<<1) */
	beq	2f

	rlwinm	r3, r0, 0, 0, 19 /* clear kexec flags, page align */
	subi	r3, r3, 4
	b	0b

2:	/* are we done? */
	rlwinm.	r7, r0, 0, 29, 29 /* IND_DONE (1<<2) */
	beq	2f
	b	3f

2:	/* is it a source page? (r9) */
	rlwinm.	r7, r0, 0, 28, 28 /* IND_SOURCE (1<<3) */
	beq	0b

	rlwinm	r9, r0, 0, 0, 19 /* clear kexec flags, page align */

	li	r7, PAGE_SIZE / 4
	mtctr   r7
	subi    r9, r9, 4
	subi    r8, r8, 4
9:
	lwzu    r0, 4(r9)  /* do the copy */
	xor	r6, r6, r0
	stwu    r0, 4(r8)
	dcbst	0, r8
	sync
	icbi	0, r8
	bdnz    9b

	addi    r9, r9, 4
	addi    r8, r8, 4
	b	0b

3:

	/* To be certain of avoiding problems with self-modifying code
	 * execute a serializing instruction here.
	 */
	isync
	sync

	mfspr	r3, SPRN_PIR /* current core we are running on */
	mr	r4, r5 /* load physical address of chunk called */

	/* jump to the entry point, usually the setup routine */
	mtlr	r5
	blrl

1:	b	1b

relocate_new_kernel_end:

	.globl relocate_new_kernel_size
relocate_new_kernel_size:
	.long relocate_new_kernel_end - relocate_new_kernel
#endif
