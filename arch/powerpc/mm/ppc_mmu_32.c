/*
 * This file contains the routines for handling the MMU on those
 * PowerPC implementations where the MMU substantially follows the
 * architecture specification.  This includes the 6xx, 7xx, 7xxx,
 * 8260, and POWER3 implementations but excludes the 8xx and 4xx.
 *  -- paulus
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/highmem.h>

#include <asm/prom.h>
#include <asm/mmu.h>
#include <asm/machdep.h>
#include <asm/lmb.h>

#include "mmu_decl.h"

struct hash_pte *Hash, *Hash_end;
unsigned long Hash_size, Hash_mask;
unsigned long _SDR1;

union ubat {			/* BAT register values to be loaded */
	struct ppc_bat bat;
	u32	word[2];
} BATS[8][2];			/* 8 pairs of IBAT, DBAT */

struct batrange {		/* stores address ranges mapped by BATs */
	unsigned long start;
	unsigned long limit;
	unsigned long phys;
} bat_addrs[8];

/*
 * Return PA for this VA if it is mapped by a BAT, or 0
 */
unsigned long v_mapped_by_bats(unsigned long va)
{
	int b;
	for (b = 0; b < 4; ++b)
		if (va >= bat_addrs[b].start && va < bat_addrs[b].limit)
			return bat_addrs[b].phys + (va - bat_addrs[b].start);
	return 0;
}

/*
 * Return VA for a given PA or 0 if not mapped
 */
unsigned long p_mapped_by_bats(unsigned long pa)
{
	int b;
	for (b = 0; b < 4; ++b)
		if (pa >= bat_addrs[b].phys
	    	    && pa < (bat_addrs[b].limit-bat_addrs[b].start)
		              +bat_addrs[b].phys)
			return bat_addrs[b].start+(pa-bat_addrs[b].phys);
	return 0;
}

unsigned long __init mmu_mapin_ram(void)
{
#ifdef CONFIG_POWER4
	return 0;
#else
	unsigned long tot, bl, done;
	unsigned long max_size = (256<<20);
	unsigned long align;

	if (__map_without_bats) {
		printk(KERN_DEBUG "RAM mapped without BATs\n");
		return 0;
	}

	/* Set up BAT2 and if necessary BAT3 to cover RAM. */

	/* Make sure we don't map a block larger than the
	   smallest alignment of the physical address. */
	/* alignment of PPC_MEMSTART */
	align = ~(PPC_MEMSTART-1) & PPC_MEMSTART;
	/* set BAT block size to MIN(max_size, align) */
	if (align && align < max_size)
		max_size = align;

	tot = total_lowmem;
	for (bl = 128<<10; bl < max_size; bl <<= 1) {
		if (bl * 2 > tot)
			break;
	}

	setbat(2, KERNELBASE, PPC_MEMSTART, bl, _PAGE_RAM);
	done = (unsigned long)bat_addrs[2].limit - KERNELBASE + 1;
	if ((done < tot) && !bat_addrs[3].limit) {
		/* use BAT3 to cover a bit more */
		tot -= done;
		for (bl = 128<<10; bl < max_size; bl <<= 1)
			if (bl * 2 > tot)
				break;
		setbat(3, KERNELBASE+done, PPC_MEMSTART+done, bl, _PAGE_RAM);
		done = (unsigned long)bat_addrs[3].limit - KERNELBASE + 1;
	}

	return done;
#endif
}

/*
 * Set up one of the I/D BAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 2 between 128k and 256M.
 */
void __init setbat(int index, unsigned long virt, unsigned long phys,
		   unsigned int size, int flags)
{
	unsigned int bl;
	int wimgxpp;
	union ubat *bat = BATS[index];

	if (((flags & _PAGE_NO_CACHE) == 0) &&
	    cpu_has_feature(CPU_FTR_NEED_COHERENT))
		flags |= _PAGE_COHERENT;

	bl = (size >> 17) - 1;
	if (PVR_VER(mfspr(SPRN_PVR)) != 1) {
		/* 603, 604, etc. */
		/* Do DBAT first */
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT | _PAGE_GUARDED);
		wimgxpp |= (flags & _PAGE_RW)? BPP_RW: BPP_RX;
		bat[1].word[0] = virt | (bl << 2) | 2; /* Vs=1, Vp=0 */
		bat[1].word[1] = phys | wimgxpp;
#ifndef CONFIG_KGDB /* want user access for breakpoints */
		if (flags & _PAGE_USER)
#endif
			bat[1].bat.batu.vp = 1;
		if (flags & _PAGE_GUARDED) {
			/* G bit must be zero in IBATs */
			bat[0].word[0] = bat[0].word[1] = 0;
		} else {
			/* make IBAT same as DBAT */
			bat[0] = bat[1];
		}
	} else {
		/* 601 cpu */
		if (bl > BL_8M)
			bl = BL_8M;
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT);
		wimgxpp |= (flags & _PAGE_RW)?
			((flags & _PAGE_USER)? PP_RWRW: PP_RWXX): PP_RXRX;
		bat->word[0] = virt | wimgxpp | 4;	/* Ks=0, Ku=1 */
		bat->word[1] = phys | bl | 0x40;	/* V=1 */
	}

	bat_addrs[index].start = virt;
	bat_addrs[index].limit = virt + ((bl + 1) << 17) - 1;
	bat_addrs[index].phys = phys;
}

/*
 * Preload a translation in the hash table
 */
void hash_preload(struct mm_struct *mm, unsigned long ea,
		  unsigned long access, unsigned long trap)
{
	pmd_t *pmd;

	if (Hash == 0)
		return;
	pmd = pmd_offset(pud_offset(pgd_offset(mm, ea), ea), ea);
	if (!pmd_none(*pmd))
		add_hash_page(mm->context.id, ea, pmd_val(*pmd));
}

/*
 * Initialize the hash table and patch the instructions in hashtable.S.
 */
void __init MMU_init_hw(void)
{
	unsigned int hmask, mb, mb2;
	unsigned int n_hpteg, lg_n_hpteg;

	extern unsigned int hash_page_patch_A[];
	extern unsigned int hash_page_patch_B[], hash_page_patch_C[];
	extern unsigned int hash_page[];
	extern unsigned int flush_hash_patch_A[], flush_hash_patch_B[];

	if (!cpu_has_feature(CPU_FTR_HPTE_TABLE)) {
		/*
		 * Put a blr (procedure return) instruction at the
		 * start of hash_page, since we can still get DSI
		 * exceptions on a 603.
		 */
		hash_page[0] = 0x4e800020;
		flush_icache_range((unsigned long) &hash_page[0],
				   (unsigned long) &hash_page[1]);
		return;
	}

	if ( ppc_md.progress ) ppc_md.progress("hash:enter", 0x105);

#define LG_HPTEG_SIZE	6		/* 64 bytes per HPTEG */
#define SDR1_LOW_BITS	((n_hpteg - 1) >> 10)
#define MIN_N_HPTEG	1024		/* min 64kB hash table */

	/*
	 * Allow 1 HPTE (1/8 HPTEG) for each page of memory.
	 * This is less than the recommended amount, but then
	 * Linux ain't AIX.
	 */
	n_hpteg = total_memory / (PAGE_SIZE * 8);
	if (n_hpteg < MIN_N_HPTEG)
		n_hpteg = MIN_N_HPTEG;
	lg_n_hpteg = __ilog2(n_hpteg);
	if (n_hpteg & (n_hpteg - 1)) {
		++lg_n_hpteg;		/* round up if not power of 2 */
		n_hpteg = 1 << lg_n_hpteg;
	}
	Hash_size = n_hpteg << LG_HPTEG_SIZE;

	/*
	 * Find some memory for the hash table.
	 */
	if ( ppc_md.progress ) ppc_md.progress("hash:find piece", 0x322);
	Hash = __va(lmb_alloc_base(Hash_size, Hash_size,
				   __initial_memory_limit));
	cacheable_memzero(Hash, Hash_size);
	_SDR1 = __pa(Hash) | SDR1_LOW_BITS;

	Hash_end = (struct hash_pte *) ((unsigned long)Hash + Hash_size);

	printk("Total memory = %ldMB; using %ldkB for hash table (at %p)\n",
	       total_memory >> 20, Hash_size >> 10, Hash);


	/*
	 * Patch up the instructions in hashtable.S:create_hpte
	 */
	if ( ppc_md.progress ) ppc_md.progress("hash:patch", 0x345);
	Hash_mask = n_hpteg - 1;
	hmask = Hash_mask >> (16 - LG_HPTEG_SIZE);
	mb2 = mb = 32 - LG_HPTEG_SIZE - lg_n_hpteg;
	if (lg_n_hpteg > 16)
		mb2 = 16 - LG_HPTEG_SIZE;

	hash_page_patch_A[0] = (hash_page_patch_A[0] & ~0xffff)
		| ((unsigned int)(Hash) >> 16);
	hash_page_patch_A[1] = (hash_page_patch_A[1] & ~0x7c0) | (mb << 6);
	hash_page_patch_A[2] = (hash_page_patch_A[2] & ~0x7c0) | (mb2 << 6);
	hash_page_patch_B[0] = (hash_page_patch_B[0] & ~0xffff) | hmask;
	hash_page_patch_C[0] = (hash_page_patch_C[0] & ~0xffff) | hmask;

	/*
	 * Ensure that the locations we've patched have been written
	 * out from the data cache and invalidated in the instruction
	 * cache, on those machines with split caches.
	 */
	flush_icache_range((unsigned long) &hash_page_patch_A[0],
			   (unsigned long) &hash_page_patch_C[1]);

	/*
	 * Patch up the instructions in hashtable.S:flush_hash_page
	 */
	flush_hash_patch_A[0] = (flush_hash_patch_A[0] & ~0xffff)
		| ((unsigned int)(Hash) >> 16);
	flush_hash_patch_A[1] = (flush_hash_patch_A[1] & ~0x7c0) | (mb << 6);
	flush_hash_patch_A[2] = (flush_hash_patch_A[2] & ~0x7c0) | (mb2 << 6);
	flush_hash_patch_B[0] = (flush_hash_patch_B[0] & ~0xffff) | hmask;
	flush_icache_range((unsigned long) &flush_hash_patch_A[0],
			   (unsigned long) &flush_hash_patch_B[1]);

	if ( ppc_md.progress ) ppc_md.progress("hash:done", 0x205);
}
