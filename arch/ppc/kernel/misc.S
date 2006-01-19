/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/sys.h>
#include <asm/unistd.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/cputable.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_8xx
#define ISYNC_8xx isync
#else
#define ISYNC_8xx
#endif
	.text

	.align	5
_GLOBAL(__delay)
	cmpwi	0,r3,0
	mtctr	r3
	beqlr
1:	bdnz	1b
	blr

/*
 * Returns (address we're running at) - (address we were linked at)
 * for use before the text and data are mapped to KERNELBASE.
 */
_GLOBAL(reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r3
	lis	r4,1b@ha
	addi	r4,r4,1b@l
	subf	r3,r4,r3
	mtlr	r0
	blr

/*
 * add_reloc_offset(x) returns x + reloc_offset().
 */
_GLOBAL(add_reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r5
	lis	r4,1b@ha
	addi	r4,r4,1b@l
	subf	r5,r4,r5
	add	r3,r3,r5
	mtlr	r0
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
 * identify_cpu,
 * called with r3 = data offset and r4 = CPU number
 * doesn't change r3
 */
_GLOBAL(identify_cpu)
	addis	r8,r3,cpu_specs@ha
	addi	r8,r8,cpu_specs@l
	mfpvr	r7
1:
	lwz	r5,CPU_SPEC_PVR_MASK(r8)
	and	r5,r5,r7
	lwz	r6,CPU_SPEC_PVR_VALUE(r8)
	cmplw	0,r6,r5
	beq	1f
	addi	r8,r8,CPU_SPEC_ENTRY_SIZE
	b	1b
1:
	addis	r6,r3,cur_cpu_spec@ha
	addi	r6,r6,cur_cpu_spec@l
	sub	r8,r8,r3
	stw	r8,0(r6)
	blr

/*
 * do_cpu_ftr_fixups - goes through the list of CPU feature fixups
 * and writes nop's over sections of code that don't apply for this cpu.
 * r3 = data offset (not changed)
 */
_GLOBAL(do_cpu_ftr_fixups)
	/* Get CPU 0 features */
	addis	r6,r3,cur_cpu_spec@ha
	addi	r6,r6,cur_cpu_spec@l
	lwz	r4,0(r6)
	add	r4,r4,r3
	lwz	r4,CPU_SPEC_FEATURES(r4)

	/* Get the fixup table */
	addis	r6,r3,__start___ftr_fixup@ha
	addi	r6,r6,__start___ftr_fixup@l
	addis	r7,r3,__stop___ftr_fixup@ha
	addi	r7,r7,__stop___ftr_fixup@l

	/* Do the fixup */
1:	cmplw	0,r6,r7
	bgelr
	addi	r6,r6,16
	lwz	r8,-16(r6)	/* mask */
	and	r8,r8,r4
	lwz	r9,-12(r6)	/* value */
	cmplw	0,r8,r9
	beq	1b
	lwz	r8,-8(r6)	/* section begin */
	lwz	r9,-4(r6)	/* section end */
	subf.	r9,r8,r9
	beq	1b
	/* write nops over the section of code */
	/* todo: if large section, add a branch at the start of it */
	srwi	r9,r9,2
	mtctr	r9
	add	r8,r8,r3
	lis	r0,0x60000000@h	/* nop */
3:	stw	r0,0(r8)
	andi.	r10,r4,CPU_FTR_SPLIT_ID_CACHE@l
	beq	2f
	dcbst	0,r8		/* suboptimal, but simpler */
	sync
	icbi	0,r8
2:	addi	r8,r8,4
	bdnz	3b
	sync			/* additional sync needed on g4 */
	isync
	b	1b

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
	cmpi	0,r5,0
	add	r5,r5,r3
	beqlr
	mtctr	r5
	bctr

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


/*
 * Flush MMU TLB
 */
_GLOBAL(_tlbia)
#if defined(CONFIG_40x)
	sync			/* Flush to memory before changing mapping */
	tlbia
	isync			/* Flush shadow TLB */
#elif defined(CONFIG_44x)
	li	r3,0
	sync

	/* Load high watermark */
	lis	r4,tlb_44x_hwater@ha
	lwz	r5,tlb_44x_hwater@l(r4)

1:	tlbwe	r3,r3,PPC44x_TLB_PAGEID
	addi	r3,r3,1
	cmpw	0,r3,r5
	ble	1b

	isync
#elif defined(CONFIG_FSL_BOOKE)
	/* Invalidate all entries in TLB0 */
	li	r3, 0x04
	tlbivax	0,3
	/* Invalidate all entries in TLB1 */
	li	r3, 0x0c
	tlbivax	0,3
	/* Invalidate all entries in TLB2 */
	li	r3, 0x14
	tlbivax	0,3
	/* Invalidate all entries in TLB3 */
	li	r3, 0x1c
	tlbivax	0,3
	msync
#ifdef CONFIG_SMP
	tlbsync
#endif /* CONFIG_SMP */
#else /* !(CONFIG_40x || CONFIG_44x || CONFIG_FSL_BOOKE) */
#if defined(CONFIG_SMP)
	rlwinm	r8,r1,0,0,18
	lwz	r8,TI_CPU(r8)
	oris	r8,r8,10
	mfmsr	r10
	SYNC
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	rlwinm	r0,r0,0,28,26		/* clear DR */
	mtmsr	r0
	SYNC_601
	isync
	lis	r9,mmu_hash_lock@h
	ori	r9,r9,mmu_hash_lock@l
	tophys(r9,r9)
10:	lwarx	r7,0,r9
	cmpwi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	sync
	tlbia
	sync
	TLBSYNC
	li	r0,0
	stw	r0,0(r9)		/* clear mmu_hash_lock */
	mtmsr	r10
	SYNC_601
	isync
#else /* CONFIG_SMP */
	sync
	tlbia
	sync
#endif /* CONFIG_SMP */
#endif /* ! defined(CONFIG_40x) */
	blr

/*
 * Flush MMU TLB for a particular address
 */
_GLOBAL(_tlbie)
#if defined(CONFIG_40x)
	tlbsx.	r3, 0, r3
	bne	10f
	sync
	/* There are only 64 TLB entries, so r3 < 64, which means bit 25 is clear.
	 * Since 25 is the V bit in the TLB_TAG, loading this value will invalidate
	 * the TLB entry. */
	tlbwe	r3, r3, TLB_TAG
	isync
10:
#elif defined(CONFIG_44x)
	mfspr	r4,SPRN_MMUCR
	mfspr	r5,SPRN_PID			/* Get PID */
	rlwimi	r4,r5,0,24,31			/* Set TID */
	mtspr	SPRN_MMUCR,r4

	tlbsx.	r3, 0, r3
	bne	10f
	sync
	/* There are only 64 TLB entries, so r3 < 64,
	 * which means bit 22, is clear.  Since 22 is
	 * the V bit in the TLB_PAGEID, loading this
	 * value will invalidate the TLB entry.
	 */
	tlbwe	r3, r3, PPC44x_TLB_PAGEID
	isync
10:
#elif defined(CONFIG_FSL_BOOKE)
	rlwinm	r4, r3, 0, 0, 19
	ori	r5, r4, 0x08	/* TLBSEL = 1 */
	ori	r6, r4, 0x10	/* TLBSEL = 2 */
	ori	r7, r4, 0x18	/* TLBSEL = 3 */
	tlbivax	0, r4
	tlbivax	0, r5
	tlbivax	0, r6
	tlbivax	0, r7
	msync
#if defined(CONFIG_SMP)
	tlbsync
#endif /* CONFIG_SMP */
#else /* !(CONFIG_40x || CONFIG_44x || CONFIG_FSL_BOOKE) */
#if defined(CONFIG_SMP)
	rlwinm	r8,r1,0,0,18
	lwz	r8,TI_CPU(r8)
	oris	r8,r8,11
	mfmsr	r10
	SYNC
	rlwinm	r0,r10,0,17,15		/* clear bit 16 (MSR_EE) */
	rlwinm	r0,r0,0,28,26		/* clear DR */
	mtmsr	r0
	SYNC_601
	isync
	lis	r9,mmu_hash_lock@h
	ori	r9,r9,mmu_hash_lock@l
	tophys(r9,r9)
10:	lwarx	r7,0,r9
	cmpwi	0,r7,0
	bne-	10b
	stwcx.	r8,0,r9
	bne-	10b
	eieio
	tlbie	r3
	sync
	TLBSYNC
	li	r0,0
	stw	r0,0(r9)		/* clear mmu_hash_lock */
	mtmsr	r10
	SYNC_601
	isync
#else /* CONFIG_SMP */
	tlbie	r3
	sync
#endif /* CONFIG_SMP */
#endif /* ! CONFIG_40x */
	blr

/*
 * Flush instruction cache.
 * This is a no-op on the 601.
 */
_GLOBAL(flush_instruction_cache)
#if defined(CONFIG_8xx)
	isync
	lis	r5, IDC_INVALL@h
	mtspr	SPRN_IC_CST, r5
#elif defined(CONFIG_4xx)
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
END_FTR_SECTION_IFCLR(CPU_FTR_SPLIT_ID_CACHE)
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
#endif /* CONFIG_8xx/4xx */
	isync
	blr

/*
 * Write any modified data cache blocks out to memory
 * and invalidate the corresponding instruction cache blocks.
 * This is a no-op on the 601.
 *
 * __flush_icache_range(unsigned long start, unsigned long stop)
 */
_GLOBAL(__flush_icache_range)
BEGIN_FTR_SECTION
	blr				/* for 601, do nothing */
END_FTR_SECTION_IFCLR(CPU_FTR_SPLIT_ID_CACHE)
	li	r5,L1_CACHE_BYTES-1
	andc	r3,r3,r5
	subf	r4,r3,r4
	add	r4,r4,r5
	srwi.	r4,r4,L1_CACHE_SHIFT
	beqlr
	mtctr	r4
	mr	r6,r3
1:	dcbst	0,r3
	addi	r3,r3,L1_CACHE_BYTES
	bdnz	1b
	sync				/* wait for dcbst's to get to ram */
	mtctr	r4
2:	icbi	0,r6
	addi	r6,r6,L1_CACHE_BYTES
	bdnz	2b
	sync				/* additional sync needed on g4 */
	isync
	blr
/*
 * Write any modified data cache blocks out to memory.
 * Does not invalidate the corresponding cache lines (especially for
 * any corresponding instruction cache).
 *
 * clean_dcache_range(unsigned long start, unsigned long stop)
 */
_GLOBAL(clean_dcache_range)
	li	r5,L1_CACHE_BYTES-1
	andc	r3,r3,r5
	subf	r4,r3,r4
	add	r4,r4,r5
	srwi.	r4,r4,L1_CACHE_SHIFT
	beqlr
	mtctr	r4

1:	dcbst	0,r3
	addi	r3,r3,L1_CACHE_BYTES
	bdnz	1b
	sync				/* wait for dcbst's to get to ram */
	blr

/*
 * Write any modified data cache blocks out to memory and invalidate them.
 * Does not invalidate the corresponding instruction cache blocks.
 *
 * flush_dcache_range(unsigned long start, unsigned long stop)
 */
_GLOBAL(flush_dcache_range)
	li	r5,L1_CACHE_BYTES-1
	andc	r3,r3,r5
	subf	r4,r3,r4
	add	r4,r4,r5
	srwi.	r4,r4,L1_CACHE_SHIFT
	beqlr
	mtctr	r4

1:	dcbf	0,r3
	addi	r3,r3,L1_CACHE_BYTES
	bdnz	1b
	sync				/* wait for dcbst's to get to ram */
	blr

/*
 * Like above, but invalidate the D-cache.  This is used by the 8xx
 * to invalidate the cache so the PPC core doesn't get stale data
 * from the CPM (no cache snooping here :-).
 *
 * invalidate_dcache_range(unsigned long start, unsigned long stop)
 */
_GLOBAL(invalidate_dcache_range)
	li	r5,L1_CACHE_BYTES-1
	andc	r3,r3,r5
	subf	r4,r3,r4
	add	r4,r4,r5
	srwi.	r4,r4,L1_CACHE_SHIFT
	beqlr
	mtctr	r4

1:	dcbi	0,r3
	addi	r3,r3,L1_CACHE_BYTES
	bdnz	1b
	sync				/* wait for dcbi's to get to ram */
	blr

#ifdef CONFIG_NOT_COHERENT_CACHE
/*
 * 40x cores have 8K or 16K dcache and 32 byte line size.
 * 44x has a 32K dcache and 32 byte line size.
 * 8xx has 1, 2, 4, 8K variants.
 * For now, cover the worst case of the 44x.
 * Must be called with external interrupts disabled.
 */
#define CACHE_NWAYS	64
#define CACHE_NLINES	16

_GLOBAL(flush_dcache_all)
	li	r4, (2 * CACHE_NWAYS * CACHE_NLINES)
	mtctr	r4
	lis     r5, KERNELBASE@h
1:	lwz	r3, 0(r5)		/* Load one word from every line */
	addi	r5, r5, L1_CACHE_BYTES
	bdnz    1b
	blr
#endif /* CONFIG_NOT_COHERENT_CACHE */

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
	blr					/* for 601, do nothing */
END_FTR_SECTION_IFCLR(CPU_FTR_SPLIT_ID_CACHE)
	rlwinm	r3,r3,0,0,19			/* Get page base address */
	li	r4,4096/L1_CACHE_BYTES	/* Number of lines in a page */
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
	isync
	blr

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
	blr					/* for 601, do nothing */
END_FTR_SECTION_IFCLR(CPU_FTR_SPLIT_ID_CACHE)
	mfmsr	r10
	rlwinm	r0,r10,0,28,26			/* clear DR */
	mtmsr	r0
	isync
	rlwinm	r3,r3,0,0,19			/* Get page base address */
	li	r4,4096/L1_CACHE_BYTES	/* Number of lines in a page */
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

/*
 * Clear pages using the dcbz instruction, which doesn't cause any
 * memory traffic (except to write out any cache lines which get
 * displaced).  This only works on cacheable memory.
 *
 * void clear_pages(void *page, int order) ;
 */
_GLOBAL(clear_pages)
	li	r0,4096/L1_CACHE_BYTES
	slw	r0,r0,r4
	mtctr	r0
#ifdef CONFIG_8xx
	li	r4, 0
1:	stw	r4, 0(r3)
	stw	r4, 4(r3)
	stw	r4, 8(r3)
	stw	r4, 12(r3)
#else
1:	dcbz	0,r3
#endif
	addi	r3,r3,L1_CACHE_BYTES
	bdnz	1b
	blr

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

#ifdef CONFIG_8xx
	/* don't use prefetch on 8xx */
    	li	r0,4096/L1_CACHE_BYTES
	mtctr	r0
1:	COPY_16_BYTES
	bdnz	1b
	blr

#else	/* not 8xx, we can prefetch */
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
	li	r0,4096/L1_CACHE_BYTES - MAX_COPY_PREFETCH
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
#endif	/* CONFIG_8xx */

/*
 * void atomic_clear_mask(atomic_t mask, atomic_t *addr)
 * void atomic_set_mask(atomic_t mask, atomic_t *addr);
 */
_GLOBAL(atomic_clear_mask)
10:	lwarx	r5,0,r4
	andc	r5,r5,r3
	PPC405_ERR77(0,r4)
	stwcx.	r5,0,r4
	bne-	10b
	blr
_GLOBAL(atomic_set_mask)
10:	lwarx	r5,0,r4
	or	r5,r5,r3
	PPC405_ERR77(0,r4)
	stwcx.	r5,0,r4
	bne-	10b
	blr

/*
 * I/O string operations
 *
 * insb(port, buf, len)
 * outsb(port, buf, len)
 * insw(port, buf, len)
 * outsw(port, buf, len)
 * insl(port, buf, len)
 * outsl(port, buf, len)
 * insw_ns(port, buf, len)
 * outsw_ns(port, buf, len)
 * insl_ns(port, buf, len)
 * outsl_ns(port, buf, len)
 *
 * The *_ns versions don't do byte-swapping.
 */
_GLOBAL(_insb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbz	r5,0(r3)
01:	eieio
02:	stbu	r5,1(r4)
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(_outsb)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,1
	blelr-
00:	lbzu	r5,1(r4)
01:	stb	r5,0(r3)
02:	eieio
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(_insw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhbrx	r5,0,r3
01:	eieio
02:	sthu	r5,2(r4)
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(_outsw)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
01:	eieio
02:	sthbrx	r5,0,r3
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(_insl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwbrx	r5,0,r3
01:	eieio
02:	stwu	r5,4(r4)
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(_outsl)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
01:	stwbrx	r5,0,r3
02:	eieio
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(__ide_mm_insw)
_GLOBAL(_insw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhz	r5,0(r3)
01:	eieio
02:	sthu	r5,2(r4)
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(__ide_mm_outsw)
_GLOBAL(_outsw_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,2
	blelr-
00:	lhzu	r5,2(r4)
01:	sth	r5,0(r3)
02:	eieio
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(__ide_mm_insl)
_GLOBAL(_insl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwz	r5,0(r3)
01:	eieio
02:	stwu	r5,4(r4)
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

_GLOBAL(__ide_mm_outsl)
_GLOBAL(_outsl_ns)
	cmpwi	0,r5,0
	mtctr	r5
	subi	r4,r4,4
	blelr-
00:	lwzu	r5,4(r4)
01:	stw	r5,0(r3)
02:	eieio
	ISYNC_8xx
	.section .fixup,"ax"
03:	blr
	.text
	.section __ex_table, "a"
		.align 2
		.long 00b, 03b
		.long 01b, 03b
		.long 02b, 03b
	.text
	bdnz	00b
	blr

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

_GLOBAL(abs)
	srawi	r4,r3,31
	xor	r3,r3,r4
	sub	r3,r3,r4
	blr

_GLOBAL(_get_SP)
	mr	r3,r1		/* Close enough */
	blr

/*
 * Create a kernel thread
 *   kernel_thread(fn, arg, flags)
 */
_GLOBAL(kernel_thread)
	stwu	r1,-16(r1)
	stw	r30,8(r1)
	stw	r31,12(r1)
	mr	r30,r3		/* function */
	mr	r31,r4		/* argument */
	ori	r3,r5,CLONE_VM	/* flags */
	oris	r3,r3,CLONE_UNTRACED>>16
	li	r4,0		/* new sp (unused) */
	li	r0,__NR_clone
	sc
	cmpwi	0,r3,0		/* parent or child? */
	bne	1f		/* return if parent */
	li	r0,0		/* make top-level stack frame */
	stwu	r0,-16(r1)
	mtlr	r30		/* fn addr in lr */
	mr	r3,r31		/* load arg and call fn */
	PPC440EP_ERR42
	blrl
	li	r0,__NR_exit	/* exit if function returns */
	li	r3,0
	sc
1:	lwz	r30,8(r1)
	lwz	r31,12(r1)
	addi	r1,r1,16
	blr

/*
 * This routine is just here to keep GCC happy - sigh...
 */
_GLOBAL(__main)
	blr

#define SYSCALL(name) \
_GLOBAL(name) \
	li	r0,__NR_##name; \
	sc; \
	bnslr; \
	lis	r4,errno@ha; \
	stw	r3,errno@l(r4); \
	li	r3,-1; \
	blr

SYSCALL(execve)

/* Why isn't this a) automatic, b) written in 'C'? */
	.data
	.align 4
_GLOBAL(sys_call_table)
	.long sys_restart_syscall /* 0 */
	.long sys_exit
	.long ppc_fork
	.long sys_read
	.long sys_write
	.long sys_open		/* 5 */
	.long sys_close
	.long sys_waitpid
	.long sys_creat
	.long sys_link
	.long sys_unlink	/* 10 */
	.long sys_execve
	.long sys_chdir
	.long sys_time
	.long sys_mknod
	.long sys_chmod		/* 15 */
	.long sys_lchown
	.long sys_ni_syscall			/* old break syscall holder */
	.long sys_stat
	.long sys_lseek
	.long sys_getpid	/* 20 */
	.long sys_mount
	.long sys_oldumount
	.long sys_setuid
	.long sys_getuid
	.long sys_stime		/* 25 */
	.long sys_ptrace
	.long sys_alarm
	.long sys_fstat
	.long sys_pause
	.long sys_utime		/* 30 */
	.long sys_ni_syscall			/* old stty syscall holder */
	.long sys_ni_syscall			/* old gtty syscall holder */
	.long sys_access
	.long sys_nice
	.long sys_ni_syscall	/* 35 */	/* old ftime syscall holder */
	.long sys_sync
	.long sys_kill
	.long sys_rename
	.long sys_mkdir
	.long sys_rmdir		/* 40 */
	.long sys_dup
	.long sys_pipe
	.long sys_times
	.long sys_ni_syscall			/* old prof syscall holder */
	.long sys_brk		/* 45 */
	.long sys_setgid
	.long sys_getgid
	.long sys_signal
	.long sys_geteuid
	.long sys_getegid	/* 50 */
	.long sys_acct
	.long sys_umount			/* recycled never used phys() */
	.long sys_ni_syscall			/* old lock syscall holder */
	.long sys_ioctl
	.long sys_fcntl		/* 55 */
	.long sys_ni_syscall			/* old mpx syscall holder */
	.long sys_setpgid
	.long sys_ni_syscall			/* old ulimit syscall holder */
	.long sys_olduname
	.long sys_umask		/* 60 */
	.long sys_chroot
	.long sys_ustat
	.long sys_dup2
	.long sys_getppid
	.long sys_getpgrp	/* 65 */
	.long sys_setsid
	.long sys_sigaction
	.long sys_sgetmask
	.long sys_ssetmask
	.long sys_setreuid	/* 70 */
	.long sys_setregid
	.long sys_sigsuspend
	.long sys_sigpending
	.long sys_sethostname
	.long sys_setrlimit	/* 75 */
	.long sys_old_getrlimit
	.long sys_getrusage
	.long sys_gettimeofday
	.long sys_settimeofday
	.long sys_getgroups	/* 80 */
	.long sys_setgroups
	.long ppc_select
	.long sys_symlink
	.long sys_lstat
	.long sys_readlink	/* 85 */
	.long sys_uselib
	.long sys_swapon
	.long sys_reboot
	.long old_readdir
	.long sys_mmap		/* 90 */
	.long sys_munmap
	.long sys_truncate
	.long sys_ftruncate
	.long sys_fchmod
	.long sys_fchown	/* 95 */
	.long sys_getpriority
	.long sys_setpriority
	.long sys_ni_syscall			/* old profil syscall holder */
	.long sys_statfs
	.long sys_fstatfs	/* 100 */
	.long sys_ni_syscall
	.long sys_socketcall
	.long sys_syslog
	.long sys_setitimer
	.long sys_getitimer	/* 105 */
	.long sys_newstat
	.long sys_newlstat
	.long sys_newfstat
	.long sys_uname
	.long sys_ni_syscall	/* 110 */
	.long sys_vhangup
	.long sys_ni_syscall	/* old 'idle' syscall */
	.long sys_ni_syscall
	.long sys_wait4
	.long sys_swapoff	/* 115 */
	.long sys_sysinfo
	.long sys_ipc
	.long sys_fsync
	.long sys_sigreturn
	.long ppc_clone		/* 120 */
	.long sys_setdomainname
	.long sys_newuname
	.long sys_ni_syscall
	.long sys_adjtimex
	.long sys_mprotect	/* 125 */
	.long sys_sigprocmask
	.long sys_ni_syscall	/* old sys_create_module */
	.long sys_init_module
	.long sys_delete_module
	.long sys_ni_syscall	/* old sys_get_kernel_syms */	/* 130 */
	.long sys_quotactl
	.long sys_getpgid
	.long sys_fchdir
	.long sys_bdflush
	.long sys_sysfs		/* 135 */
	.long sys_personality
	.long sys_ni_syscall	/* for afs_syscall */
	.long sys_setfsuid
	.long sys_setfsgid
	.long sys_llseek	/* 140 */
	.long sys_getdents
	.long ppc_select
	.long sys_flock
	.long sys_msync
	.long sys_readv		/* 145 */
	.long sys_writev
	.long sys_getsid
	.long sys_fdatasync
	.long sys_sysctl
	.long sys_mlock		/* 150 */
	.long sys_munlock
	.long sys_mlockall
	.long sys_munlockall
	.long sys_sched_setparam
	.long sys_sched_getparam	/* 155 */
	.long sys_sched_setscheduler
	.long sys_sched_getscheduler
	.long sys_sched_yield
	.long sys_sched_get_priority_max
	.long sys_sched_get_priority_min  /* 160 */
	.long sys_sched_rr_get_interval
	.long sys_nanosleep
	.long sys_mremap
	.long sys_setresuid
	.long sys_getresuid	/* 165 */
	.long sys_ni_syscall		/* old sys_query_module */
	.long sys_poll
	.long sys_nfsservctl
	.long sys_setresgid
	.long sys_getresgid	/* 170 */
	.long sys_prctl
	.long sys_rt_sigreturn
	.long sys_rt_sigaction
	.long sys_rt_sigprocmask
	.long sys_rt_sigpending	/* 175 */
	.long sys_rt_sigtimedwait
	.long sys_rt_sigqueueinfo
	.long sys_rt_sigsuspend
	.long sys_pread64
	.long sys_pwrite64	/* 180 */
	.long sys_chown
	.long sys_getcwd
	.long sys_capget
	.long sys_capset
	.long sys_sigaltstack	/* 185 */
	.long sys_sendfile
	.long sys_ni_syscall		/* streams1 */
	.long sys_ni_syscall		/* streams2 */
	.long ppc_vfork
	.long sys_getrlimit	/* 190 */
	.long sys_readahead
	.long sys_mmap2
	.long sys_truncate64
	.long sys_ftruncate64
	.long sys_stat64	/* 195 */
	.long sys_lstat64
	.long sys_fstat64
	.long sys_pciconfig_read
	.long sys_pciconfig_write
	.long sys_pciconfig_iobase 	/* 200 */
	.long sys_ni_syscall		/* 201 - reserved - MacOnLinux - new */
	.long sys_getdents64
	.long sys_pivot_root
	.long sys_fcntl64
	.long sys_madvise	/* 205 */
	.long sys_mincore
	.long sys_gettid
	.long sys_tkill
	.long sys_setxattr
	.long sys_lsetxattr	/* 210 */
	.long sys_fsetxattr
	.long sys_getxattr
	.long sys_lgetxattr
	.long sys_fgetxattr
	.long sys_listxattr	/* 215 */
	.long sys_llistxattr
	.long sys_flistxattr
	.long sys_removexattr
	.long sys_lremovexattr
	.long sys_fremovexattr	/* 220 */
	.long sys_futex
	.long sys_sched_setaffinity
	.long sys_sched_getaffinity
	.long sys_ni_syscall
	.long sys_ni_syscall	/* 225 - reserved for Tux */
	.long sys_sendfile64
	.long sys_io_setup
	.long sys_io_destroy
	.long sys_io_getevents
	.long sys_io_submit	/* 230 */
	.long sys_io_cancel
	.long sys_set_tid_address
	.long sys_fadvise64
	.long sys_exit_group
	.long sys_lookup_dcookie /* 235 */
	.long sys_epoll_create
	.long sys_epoll_ctl
	.long sys_epoll_wait
	.long sys_remap_file_pages
	.long sys_timer_create	/* 240 */
	.long sys_timer_settime
	.long sys_timer_gettime
	.long sys_timer_getoverrun
	.long sys_timer_delete
	.long sys_clock_settime	/* 245 */
	.long sys_clock_gettime
	.long sys_clock_getres
	.long sys_clock_nanosleep
	.long sys_swapcontext
	.long sys_tgkill	/* 250 */
	.long sys_utimes
	.long sys_statfs64
	.long sys_fstatfs64
	.long ppc_fadvise64_64
	.long sys_ni_syscall		/* 255 - rtas (used on ppc64) */
	.long sys_debug_setcontext
	.long sys_ni_syscall		/* 257 reserved for vserver */
	.long sys_ni_syscall		/* 258 reserved for new sys_remap_file_pages */
	.long sys_ni_syscall		/* 259 reserved for new sys_mbind */
	.long sys_ni_syscall		/* 260 reserved for new sys_get_mempolicy */
	.long sys_ni_syscall		/* 261 reserved for new sys_set_mempolicy */
	.long sys_mq_open
	.long sys_mq_unlink
	.long sys_mq_timedsend
	.long sys_mq_timedreceive	/* 265 */
	.long sys_mq_notify
	.long sys_mq_getsetattr
	.long sys_kexec_load
	.long sys_add_key
	.long sys_request_key		/* 270 */
	.long sys_keyctl
	.long sys_waitid
	.long sys_ioprio_set
	.long sys_ioprio_get
	.long sys_inotify_init		/* 275 */
	.long sys_inotify_add_watch
	.long sys_inotify_rm_watch
