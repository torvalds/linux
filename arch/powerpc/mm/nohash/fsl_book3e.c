// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Modifications by Kumar Gala (galak@kernel.crashing.org) to support
 * E500 Book E processors.
 *
 * Copyright 2004,2010 Freescale Semiconductor, Inc.
 *
 * This file contains the routines for initializing the MMU
 * on the 4xx series of chips.
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
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/memblock.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/mmu.h>
#include <linux/uaccess.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/setup.h>
#include <asm/paca.h>

#include <mm/mmu_decl.h>

unsigned int tlbcam_index;

#define NUM_TLBCAMS	(64)
struct tlbcam TLBCAM[NUM_TLBCAMS];

struct tlbcamrange {
	unsigned long start;
	unsigned long limit;
	phys_addr_t phys;
} tlbcam_addrs[NUM_TLBCAMS];

#ifdef CONFIG_FSL_BOOKE
/*
 * Return PA for this VA if it is mapped by a CAM, or 0
 */
phys_addr_t v_block_mapped(unsigned long va)
{
	int b;
	for (b = 0; b < tlbcam_index; ++b)
		if (va >= tlbcam_addrs[b].start && va < tlbcam_addrs[b].limit)
			return tlbcam_addrs[b].phys + (va - tlbcam_addrs[b].start);
	return 0;
}

/*
 * Return VA for a given PA or 0 if not mapped
 */
unsigned long p_block_mapped(phys_addr_t pa)
{
	int b;
	for (b = 0; b < tlbcam_index; ++b)
		if (pa >= tlbcam_addrs[b].phys
			&& pa < (tlbcam_addrs[b].limit-tlbcam_addrs[b].start)
		              +tlbcam_addrs[b].phys)
			return tlbcam_addrs[b].start+(pa-tlbcam_addrs[b].phys);
	return 0;
}
#endif

/*
 * Set up a variable-size TLB entry (tlbcam). The parameters are not checked;
 * in particular size must be a power of 4 between 4k and the max supported by
 * an implementation; max may further be limited by what can be represented in
 * an unsigned long (for example, 32-bit implementations cannot support a 4GB
 * size).
 */
static void settlbcam(int index, unsigned long virt, phys_addr_t phys,
		unsigned long size, unsigned long flags, unsigned int pid)
{
	unsigned int tsize;

	tsize = __ilog2(size) - 10;

#if defined(CONFIG_SMP) || defined(CONFIG_PPC_E500MC)
	if ((flags & _PAGE_NO_CACHE) == 0)
		flags |= _PAGE_COHERENT;
#endif

	TLBCAM[index].MAS0 = MAS0_TLBSEL(1) | MAS0_ESEL(index) | MAS0_NV(index+1);
	TLBCAM[index].MAS1 = MAS1_VALID | MAS1_IPROT | MAS1_TSIZE(tsize) | MAS1_TID(pid);
	TLBCAM[index].MAS2 = virt & PAGE_MASK;

	TLBCAM[index].MAS2 |= (flags & _PAGE_WRITETHRU) ? MAS2_W : 0;
	TLBCAM[index].MAS2 |= (flags & _PAGE_NO_CACHE) ? MAS2_I : 0;
	TLBCAM[index].MAS2 |= (flags & _PAGE_COHERENT) ? MAS2_M : 0;
	TLBCAM[index].MAS2 |= (flags & _PAGE_GUARDED) ? MAS2_G : 0;
	TLBCAM[index].MAS2 |= (flags & _PAGE_ENDIAN) ? MAS2_E : 0;

	TLBCAM[index].MAS3 = (phys & MAS3_RPN) | MAS3_SR;
	TLBCAM[index].MAS3 |= (flags & _PAGE_RW) ? MAS3_SW : 0;
	if (mmu_has_feature(MMU_FTR_BIG_PHYS))
		TLBCAM[index].MAS7 = (u64)phys >> 32;

	/* Below is unlikely -- only for large user pages or similar */
	if (pte_user(__pte(flags))) {
		TLBCAM[index].MAS3 |= MAS3_UR;
		TLBCAM[index].MAS3 |= (flags & _PAGE_EXEC) ? MAS3_UX : 0;
		TLBCAM[index].MAS3 |= (flags & _PAGE_RW) ? MAS3_UW : 0;
	} else {
		TLBCAM[index].MAS3 |= (flags & _PAGE_EXEC) ? MAS3_SX : 0;
	}

	tlbcam_addrs[index].start = virt;
	tlbcam_addrs[index].limit = virt + size - 1;
	tlbcam_addrs[index].phys = phys;
}

unsigned long calc_cam_sz(unsigned long ram, unsigned long virt,
			  phys_addr_t phys)
{
	unsigned int camsize = __ilog2(ram);
	unsigned int align = __ffs(virt | phys);
	unsigned long max_cam;

	if ((mfspr(SPRN_MMUCFG) & MMUCFG_MAVN) == MMUCFG_MAVN_V1) {
		/* Convert (4^max) kB to (2^max) bytes */
		max_cam = ((mfspr(SPRN_TLB1CFG) >> 16) & 0xf) * 2 + 10;
		camsize &= ~1U;
		align &= ~1U;
	} else {
		/* Convert (2^max) kB to (2^max) bytes */
		max_cam = __ilog2(mfspr(SPRN_TLB1PS)) + 10;
	}

	if (camsize > align)
		camsize = align;
	if (camsize > max_cam)
		camsize = max_cam;

	return 1UL << camsize;
}

static unsigned long map_mem_in_cams_addr(phys_addr_t phys, unsigned long virt,
					unsigned long ram, int max_cam_idx,
					bool dryrun, bool init)
{
	int i;
	unsigned long amount_mapped = 0;
	unsigned long boundary;

	if (strict_kernel_rwx_enabled())
		boundary = (unsigned long)(_sinittext - _stext);
	else
		boundary = ram;

	/* Calculate CAM values */
	for (i = 0; boundary && i < max_cam_idx; i++) {
		unsigned long cam_sz;
		pgprot_t prot = init ? PAGE_KERNEL_X : PAGE_KERNEL_ROX;

		cam_sz = calc_cam_sz(boundary, virt, phys);
		if (!dryrun)
			settlbcam(i, virt, phys, cam_sz, pgprot_val(prot), 0);

		boundary -= cam_sz;
		amount_mapped += cam_sz;
		virt += cam_sz;
		phys += cam_sz;
	}
	for (ram -= amount_mapped; ram && i < max_cam_idx; i++) {
		unsigned long cam_sz;
		pgprot_t prot = init ? PAGE_KERNEL_X : PAGE_KERNEL;

		cam_sz = calc_cam_sz(ram, virt, phys);
		if (!dryrun)
			settlbcam(i, virt, phys, cam_sz, pgprot_val(prot), 0);

		ram -= cam_sz;
		amount_mapped += cam_sz;
		virt += cam_sz;
		phys += cam_sz;
	}

	if (dryrun)
		return amount_mapped;

	if (init) {
		loadcam_multi(0, i, max_cam_idx);
		tlbcam_index = i;
	} else {
		loadcam_multi(0, i, 0);
		WARN_ON(i > tlbcam_index);
	}

#ifdef CONFIG_PPC64
	get_paca()->tcd.esel_next = i;
	get_paca()->tcd.esel_max = mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY;
	get_paca()->tcd.esel_first = i;
#endif

	return amount_mapped;
}

unsigned long map_mem_in_cams(unsigned long ram, int max_cam_idx, bool dryrun, bool init)
{
	unsigned long virt = PAGE_OFFSET;
	phys_addr_t phys = memstart_addr;

	return map_mem_in_cams_addr(phys, virt, ram, max_cam_idx, dryrun, init);
}

#ifdef CONFIG_PPC32

#if defined(CONFIG_LOWMEM_CAM_NUM_BOOL) && (CONFIG_LOWMEM_CAM_NUM >= NUM_TLBCAMS)
#error "LOWMEM_CAM_NUM must be less than NUM_TLBCAMS"
#endif

unsigned long __init mmu_mapin_ram(unsigned long base, unsigned long top)
{
	return tlbcam_addrs[tlbcam_index - 1].limit - PAGE_OFFSET + 1;
}

void flush_instruction_cache(void)
{
	unsigned long tmp;

	tmp = mfspr(SPRN_L1CSR1);
	tmp |= L1CSR1_ICFI | L1CSR1_ICLFR;
	mtspr(SPRN_L1CSR1, tmp);
	isync();
}

/*
 * MMU_init_hw does the chip-specific initialization of the MMU hardware.
 */
void __init MMU_init_hw(void)
{
	flush_instruction_cache();
}

static unsigned long __init tlbcam_sz(int idx)
{
	return tlbcam_addrs[idx].limit - tlbcam_addrs[idx].start + 1;
}

void __init adjust_total_lowmem(void)
{
	unsigned long ram;
	int i;

	/* adjust lowmem size to __max_low_memory */
	ram = min((phys_addr_t)__max_low_memory, (phys_addr_t)total_lowmem);

	i = switch_to_as1();
	__max_low_memory = map_mem_in_cams(ram, CONFIG_LOWMEM_CAM_NUM, false, true);
	restore_to_as0(i, 0, 0, 1);

	pr_info("Memory CAM mapping: ");
	for (i = 0; i < tlbcam_index - 1; i++)
		pr_cont("%lu/", tlbcam_sz(i) >> 20);
	pr_cont("%lu Mb, residual: %dMb\n", tlbcam_sz(tlbcam_index - 1) >> 20,
	        (unsigned int)((total_lowmem - __max_low_memory) >> 20));

	memblock_set_current_limit(memstart_addr + __max_low_memory);
}

#ifdef CONFIG_STRICT_KERNEL_RWX
void mmu_mark_rodata_ro(void)
{
	/* Everything is done in mmu_mark_initmem_nx() */
}
#endif

void mmu_mark_initmem_nx(void)
{
	unsigned long remapped;

	if (!strict_kernel_rwx_enabled())
		return;

	remapped = map_mem_in_cams(__max_low_memory, CONFIG_LOWMEM_CAM_NUM, false, false);

	WARN_ON(__max_low_memory != remapped);
}

void setup_initial_memory_limit(phys_addr_t first_memblock_base,
				phys_addr_t first_memblock_size)
{
	phys_addr_t limit = first_memblock_base + first_memblock_size;

	/* 64M mapped initially according to head_fsl_booke.S */
	memblock_set_current_limit(min_t(u64, limit, 0x04000000));
}

#ifdef CONFIG_RELOCATABLE
int __initdata is_second_reloc;
notrace void __init relocate_init(u64 dt_ptr, phys_addr_t start)
{
	unsigned long base = kernstart_virt_addr;
	phys_addr_t size;

	kernstart_addr = start;
	if (is_second_reloc) {
		virt_phys_offset = PAGE_OFFSET - memstart_addr;
		kaslr_late_init();
		return;
	}

	/*
	 * Relocatable kernel support based on processing of dynamic
	 * relocation entries. Before we get the real memstart_addr,
	 * We will compute the virt_phys_offset like this:
	 * virt_phys_offset = stext.run - kernstart_addr
	 *
	 * stext.run = (KERNELBASE & ~0x3ffffff) +
	 *				(kernstart_addr & 0x3ffffff)
	 * When we relocate, we have :
	 *
	 *	(kernstart_addr & 0x3ffffff) = (stext.run & 0x3ffffff)
	 *
	 * hence:
	 *  virt_phys_offset = (KERNELBASE & ~0x3ffffff) -
	 *                              (kernstart_addr & ~0x3ffffff)
	 *
	 */
	start &= ~0x3ffffff;
	base &= ~0x3ffffff;
	virt_phys_offset = base - start;
	early_get_first_memblock_info(__va(dt_ptr), &size);
	/*
	 * We now get the memstart_addr, then we should check if this
	 * address is the same as what the PAGE_OFFSET map to now. If
	 * not we have to change the map of PAGE_OFFSET to memstart_addr
	 * and do a second relocation.
	 */
	if (start != memstart_addr) {
		int n;
		long offset = start - memstart_addr;

		is_second_reloc = 1;
		n = switch_to_as1();
		/* map a 64M area for the second relocation */
		if (memstart_addr > start)
			map_mem_in_cams(0x4000000, CONFIG_LOWMEM_CAM_NUM,
					false, true);
		else
			map_mem_in_cams_addr(start, PAGE_OFFSET + offset,
					0x4000000, CONFIG_LOWMEM_CAM_NUM,
					false, true);
		restore_to_as0(n, offset, __va(dt_ptr), 1);
		/* We should never reach here */
		panic("Relocation error");
	}

	kaslr_early_init(__va(dt_ptr), size);
}
#endif
#endif
