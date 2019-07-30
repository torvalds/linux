/*
 * This file contains the routines for handling the MMU on those
 * PowerPC implementations where the MMU substantially follows the
 * architecture specification.  This includes the 6xx, 7xx, 7xxx,
 * and 8260 implementations but excludes the 8xx and 4xx.
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
#include <linux/memblock.h>

#include <asm/prom.h>
#include <asm/mmu.h>
#include <asm/machdep.h>
#include <asm/code-patching.h>
#include <asm/sections.h>

#include "mmu_decl.h"

struct hash_pte *Hash, *Hash_end;
unsigned long Hash_size, Hash_mask;
unsigned long _SDR1;

struct ppc_bat BATS[8][2];	/* 8 pairs of IBAT, DBAT */

struct batrange {		/* stores address ranges mapped by BATs */
	unsigned long start;
	unsigned long limit;
	phys_addr_t phys;
} bat_addrs[8];

/*
 * Return PA for this VA if it is mapped by a BAT, or 0
 */
phys_addr_t v_block_mapped(unsigned long va)
{
	int b;
	for (b = 0; b < ARRAY_SIZE(bat_addrs); ++b)
		if (va >= bat_addrs[b].start && va < bat_addrs[b].limit)
			return bat_addrs[b].phys + (va - bat_addrs[b].start);
	return 0;
}

/*
 * Return VA for a given PA or 0 if not mapped
 */
unsigned long p_block_mapped(phys_addr_t pa)
{
	int b;
	for (b = 0; b < ARRAY_SIZE(bat_addrs); ++b)
		if (pa >= bat_addrs[b].phys
	    	    && pa < (bat_addrs[b].limit-bat_addrs[b].start)
		              +bat_addrs[b].phys)
			return bat_addrs[b].start+(pa-bat_addrs[b].phys);
	return 0;
}

static int find_free_bat(void)
{
	int b;

	if (cpu_has_feature(CPU_FTR_601)) {
		for (b = 0; b < 4; b++) {
			struct ppc_bat *bat = BATS[b];

			if (!(bat[0].batl & 0x40))
				return b;
		}
	} else {
		int n = mmu_has_feature(MMU_FTR_USE_HIGH_BATS) ? 8 : 4;

		for (b = 0; b < n; b++) {
			struct ppc_bat *bat = BATS[b];

			if (!(bat[1].batu & 3))
				return b;
		}
	}
	return -1;
}

/*
 * This function calculates the size of the larger block usable to map the
 * beginning of an area based on the start address and size of that area:
 * - max block size is 8M on 601 and 256 on other 6xx.
 * - base address must be aligned to the block size. So the maximum block size
 *   is identified by the lowest bit set to 1 in the base address (for instance
 *   if base is 0x16000000, max size is 0x02000000).
 * - block size has to be a power of two. This is calculated by finding the
 *   highest bit set to 1.
 */
static unsigned int block_size(unsigned long base, unsigned long top)
{
	unsigned int max_size = (cpu_has_feature(CPU_FTR_601) ? 8 : 256) << 20;
	unsigned int base_shift = (ffs(base) - 1) & 31;
	unsigned int block_shift = (fls(top - base) - 1) & 31;

	return min3(max_size, 1U << base_shift, 1U << block_shift);
}

/*
 * Set up one of the IBAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 2 between 128k and 256M.
 * Only for 603+ ...
 */
static void setibat(int index, unsigned long virt, phys_addr_t phys,
		    unsigned int size, pgprot_t prot)
{
	unsigned int bl = (size >> 17) - 1;
	int wimgxpp;
	struct ppc_bat *bat = BATS[index];
	unsigned long flags = pgprot_val(prot);

	if (!cpu_has_feature(CPU_FTR_NEED_COHERENT))
		flags &= ~_PAGE_COHERENT;

	wimgxpp = (flags & _PAGE_COHERENT) | (_PAGE_EXEC ? BPP_RX : BPP_XX);
	bat[0].batu = virt | (bl << 2) | 2; /* Vs=1, Vp=0 */
	bat[0].batl = BAT_PHYS_ADDR(phys) | wimgxpp;
	if (flags & _PAGE_USER)
		bat[0].batu |= 1;	/* Vp = 1 */
}

static void clearibat(int index)
{
	struct ppc_bat *bat = BATS[index];

	bat[0].batu = 0;
	bat[0].batl = 0;
}

static unsigned long __init __mmu_mapin_ram(unsigned long base, unsigned long top)
{
	int idx;

	while ((idx = find_free_bat()) != -1 && base != top) {
		unsigned int size = block_size(base, top);

		if (size < 128 << 10)
			break;
		setbat(idx, PAGE_OFFSET + base, base, size, PAGE_KERNEL_X);
		base += size;
	}

	return base;
}

unsigned long __init mmu_mapin_ram(unsigned long base, unsigned long top)
{
	unsigned long done;
	unsigned long border = (unsigned long)__init_begin - PAGE_OFFSET;

	if (__map_without_bats) {
		pr_debug("RAM mapped without BATs\n");
		return base;
	}

	if (!strict_kernel_rwx_enabled() || base >= border || top <= border)
		return __mmu_mapin_ram(base, top);

	done = __mmu_mapin_ram(base, border);
	if (done != border)
		return done;

	return __mmu_mapin_ram(border, top);
}

void mmu_mark_initmem_nx(void)
{
	int nb = mmu_has_feature(MMU_FTR_USE_HIGH_BATS) ? 8 : 4;
	int i;
	unsigned long base = (unsigned long)_stext - PAGE_OFFSET;
	unsigned long top = (unsigned long)_etext - PAGE_OFFSET;
	unsigned long size;

	if (cpu_has_feature(CPU_FTR_601))
		return;

	for (i = 0; i < nb - 1 && base < top && top - base > (128 << 10);) {
		size = block_size(base, top);
		setibat(i++, PAGE_OFFSET + base, base, size, PAGE_KERNEL_TEXT);
		base += size;
	}
	if (base < top) {
		size = block_size(base, top);
		size = max(size, 128UL << 10);
		if ((top - base) > size) {
			if (strict_kernel_rwx_enabled())
				pr_warn("Kernel _etext not properly aligned\n");
			size <<= 1;
		}
		setibat(i++, PAGE_OFFSET + base, base, size, PAGE_KERNEL_TEXT);
		base += size;
	}
	for (; i < nb; i++)
		clearibat(i);

	update_bats();

	for (i = TASK_SIZE >> 28; i < 16; i++) {
		/* Do not set NX on VM space for modules */
		if (IS_ENABLED(CONFIG_MODULES) &&
		    (VMALLOC_START & 0xf0000000) == i << 28)
			break;
		mtsrin(mfsrin(i << 28) | 0x10000000, i << 28);
	}
}

void mmu_mark_rodata_ro(void)
{
	int nb = mmu_has_feature(MMU_FTR_USE_HIGH_BATS) ? 8 : 4;
	int i;

	if (cpu_has_feature(CPU_FTR_601))
		return;

	for (i = 0; i < nb; i++) {
		struct ppc_bat *bat = BATS[i];

		if (bat_addrs[i].start < (unsigned long)__init_begin)
			bat[1].batl = (bat[1].batl & ~BPP_RW) | BPP_RX;
	}

	update_bats();
}

/*
 * Set up one of the I/D BAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 2 between 128k and 256M.
 * On 603+, only set IBAT when _PAGE_EXEC is set
 */
void __init setbat(int index, unsigned long virt, phys_addr_t phys,
		   unsigned int size, pgprot_t prot)
{
	unsigned int bl;
	int wimgxpp;
	struct ppc_bat *bat = BATS[index];
	unsigned long flags = pgprot_val(prot);

	if ((flags & _PAGE_NO_CACHE) ||
	    (cpu_has_feature(CPU_FTR_NEED_COHERENT) == 0))
		flags &= ~_PAGE_COHERENT;

	bl = (size >> 17) - 1;
	if (PVR_VER(mfspr(SPRN_PVR)) != 1) {
		/* 603, 604, etc. */
		/* Do DBAT first */
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT | _PAGE_GUARDED);
		wimgxpp |= (flags & _PAGE_RW)? BPP_RW: BPP_RX;
		bat[1].batu = virt | (bl << 2) | 2; /* Vs=1, Vp=0 */
		bat[1].batl = BAT_PHYS_ADDR(phys) | wimgxpp;
		if (flags & _PAGE_USER)
			bat[1].batu |= 1; 	/* Vp = 1 */
		if (flags & _PAGE_GUARDED) {
			/* G bit must be zero in IBATs */
			flags &= ~_PAGE_EXEC;
		}
		if (flags & _PAGE_EXEC)
			bat[0] = bat[1];
		else
			bat[0].batu = bat[0].batl = 0;
	} else {
		/* 601 cpu */
		if (bl > BL_8M)
			bl = BL_8M;
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT);
		wimgxpp |= (flags & _PAGE_RW)?
			((flags & _PAGE_USER)? PP_RWRW: PP_RWXX): PP_RXRX;
		bat->batu = virt | wimgxpp | 4;	/* Ks=0, Ku=1 */
		bat->batl = phys | bl | 0x40;	/* V=1 */
	}

	bat_addrs[index].start = virt;
	bat_addrs[index].limit = virt + ((bl + 1) << 17) - 1;
	bat_addrs[index].phys = phys;
}

/*
 * Preload a translation in the hash table
 */
void hash_preload(struct mm_struct *mm, unsigned long ea,
		  bool is_exec, unsigned long trap)
{
	pmd_t *pmd;

	if (!Hash)
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

	if (!mmu_has_feature(MMU_FTR_HPTE_TABLE))
		return;

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
	Hash = memblock_alloc(Hash_size, Hash_size);
	if (!Hash)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, Hash_size, Hash_size);
	_SDR1 = __pa(Hash) | SDR1_LOW_BITS;

	Hash_end = (struct hash_pte *) ((unsigned long)Hash + Hash_size);

	printk("Total memory = %lldMB; using %ldkB for hash table (at %p)\n",
	       (unsigned long long)(total_memory >> 20), Hash_size >> 10, Hash);


	/*
	 * Patch up the instructions in hashtable.S:create_hpte
	 */
	if ( ppc_md.progress ) ppc_md.progress("hash:patch", 0x345);
	Hash_mask = n_hpteg - 1;
	hmask = Hash_mask >> (16 - LG_HPTEG_SIZE);
	mb2 = mb = 32 - LG_HPTEG_SIZE - lg_n_hpteg;
	if (lg_n_hpteg > 16)
		mb2 = 16 - LG_HPTEG_SIZE;

	modify_instruction_site(&patch__hash_page_A0, 0xffff,
				((unsigned int)Hash - PAGE_OFFSET) >> 16);
	modify_instruction_site(&patch__hash_page_A1, 0x7c0, mb << 6);
	modify_instruction_site(&patch__hash_page_A2, 0x7c0, mb2 << 6);
	modify_instruction_site(&patch__hash_page_B, 0xffff, hmask);
	modify_instruction_site(&patch__hash_page_C, 0xffff, hmask);

	/*
	 * Patch up the instructions in hashtable.S:flush_hash_page
	 */
	modify_instruction_site(&patch__flush_hash_A0, 0xffff,
				((unsigned int)Hash - PAGE_OFFSET) >> 16);
	modify_instruction_site(&patch__flush_hash_A1, 0x7c0, mb << 6);
	modify_instruction_site(&patch__flush_hash_A2, 0x7c0, mb2 << 6);
	modify_instruction_site(&patch__flush_hash_B, 0xffff, hmask);

	if ( ppc_md.progress ) ppc_md.progress("hash:done", 0x205);
}

void setup_initial_memory_limit(phys_addr_t first_memblock_base,
				phys_addr_t first_memblock_size)
{
	/* We don't currently support the first MEMBLOCK not mapping 0
	 * physical on those processors
	 */
	BUG_ON(first_memblock_base != 0);

	/* 601 can only access 16MB at the moment */
	if (PVR_VER(mfspr(SPRN_PVR)) == 1)
		memblock_set_current_limit(min_t(u64, first_memblock_size, 0x01000000));
	else /* Anything else has 256M mapped */
		memblock_set_current_limit(min_t(u64, first_memblock_size, 0x10000000));
}
