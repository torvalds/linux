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

_GLOBAL(kernel_execve)
	li	r0,__NR_execve
	sc
	bnslr
	neg	r3,r3
	blr

/*
 * This routine is just here to keep GCC happy - sigh...
 */
_GLOBAL(__main)
	blr

