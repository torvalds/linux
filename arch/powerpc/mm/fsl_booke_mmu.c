/*
 * Modifications by Kumar Gala (galak@kernel.crashing.org) to support
 * E500 Book E processors.
 *
 * Copyright 2004 Freescale Semiconductor, Inc
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
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
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

#include <asm/pgalloc.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/uaccess.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/setup.h>

#include "mmu_decl.h"

unsigned int tlbcam_index;

#define NUM_TLBCAMS	(64)

#if defined(CONFIG_LOWMEM_CAM_NUM_BOOL) && (CONFIG_LOWMEM_CAM_NUM >= NUM_TLBCAMS)
#error "LOWMEM_CAM_NUM must be less than NUM_TLBCAMS"
#endif

struct tlbcam {
	u32	MAS0;
	u32	MAS1;
	unsigned long	MAS2;
	u32	MAS3;
	u32	MAS7;
} TLBCAM[NUM_TLBCAMS];

struct tlbcamrange {
	unsigned long start;
	unsigned long limit;
	phys_addr_t phys;
} tlbcam_addrs[NUM_TLBCAMS];

extern unsigned int tlbcam_index;

unsigned long tlbcam_sz(int idx)
{
	return tlbcam_addrs[idx].limit - tlbcam_addrs[idx].start + 1;
}

/*
 * Return PA for this VA if it is mapped by a CAM, or 0
 */
phys_addr_t v_mapped_by_tlbcam(unsigned long va)
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
unsigned long p_mapped_by_tlbcam(phys_addr_t pa)
{
	int b;
	for (b = 0; b < tlbcam_index; ++b)
		if (pa >= tlbcam_addrs[b].phys
			&& pa < (tlbcam_addrs[b].limit-tlbcam_addrs[b].start)
		              +tlbcam_addrs[b].phys)
			return tlbcam_addrs[b].start+(pa-tlbcam_addrs[b].phys);
	return 0;
}

void loadcam_entry(int idx)
{
	mtspr(SPRN_MAS0, TLBCAM[idx].MAS0);
	mtspr(SPRN_MAS1, TLBCAM[idx].MAS1);
	mtspr(SPRN_MAS2, TLBCAM[idx].MAS2);
	mtspr(SPRN_MAS3, TLBCAM[idx].MAS3);

	if (mmu_has_feature(MMU_FTR_BIG_PHYS))
		mtspr(SPRN_MAS7, TLBCAM[idx].MAS7);

	asm volatile("isync;tlbwe;isync" : : : "memory");
}

/*
 * Set up one of the I/D BAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 4 between 4k and 256M.
 */
static void settlbcam(int index, unsigned long virt, phys_addr_t phys,
		unsigned long size, unsigned long flags, unsigned int pid)
{
	unsigned int tsize, lz;

	asm (PPC_CNTLZL "%0,%1" : "=r" (lz) : "r" (size));
	tsize = 21 - lz;

#ifdef CONFIG_SMP
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

	TLBCAM[index].MAS3 = (phys & MAS3_RPN) | MAS3_SX | MAS3_SR;
	TLBCAM[index].MAS3 |= ((flags & _PAGE_RW) ? MAS3_SW : 0);
	if (mmu_has_feature(MMU_FTR_BIG_PHYS))
		TLBCAM[index].MAS7 = (u64)phys >> 32;

	if (flags & _PAGE_USER) {
	   TLBCAM[index].MAS3 |= MAS3_UX | MAS3_UR;
	   TLBCAM[index].MAS3 |= ((flags & _PAGE_RW) ? MAS3_UW : 0);
	}

	tlbcam_addrs[index].start = virt;
	tlbcam_addrs[index].limit = virt + size - 1;
	tlbcam_addrs[index].phys = phys;

	loadcam_entry(index);
}

unsigned long map_mem_in_cams(unsigned long ram, int max_cam_idx)
{
	int i;
	unsigned long virt = PAGE_OFFSET;
	phys_addr_t phys = memstart_addr;
	unsigned long amount_mapped = 0;
	unsigned long max_cam = (mfspr(SPRN_TLB1CFG) >> 16) & 0xf;

	/* Convert (4^max) kB to (2^max) bytes */
	max_cam = max_cam * 2 + 10;

	/* Calculate CAM values */
	for (i = 0; ram && i < max_cam_idx; i++) {
		unsigned int camsize = __ilog2(ram) & ~1U;
		unsigned int align = __ffs(virt | phys) & ~1U;
		unsigned long cam_sz;

		if (camsize > align)
			camsize = align;
		if (camsize > max_cam)
			camsize = max_cam;

		cam_sz = 1UL << camsize;
		settlbcam(i, virt, phys, cam_sz, PAGE_KERNEL_X, 0);

		ram -= cam_sz;
		amount_mapped += cam_sz;
		virt += cam_sz;
		phys += cam_sz;
	}
	tlbcam_index = i;

	return amount_mapped;
}

unsigned long __init mmu_mapin_ram(unsigned long top)
{
	return tlbcam_addrs[tlbcam_index - 1].limit - PAGE_OFFSET + 1;
}

/*
 * MMU_init_hw does the chip-specific initialization of the MMU hardware.
 */
void __init MMU_init_hw(void)
{
	flush_instruction_cache();
}

void __init adjust_total_lowmem(void)
{
	unsigned long ram;
	int i;

	/* adjust lowmem size to __max_low_memory */
	ram = min((phys_addr_t)__max_low_memory, (phys_addr_t)total_lowmem);

	__max_low_memory = map_mem_in_cams(ram, CONFIG_LOWMEM_CAM_NUM);

	pr_info("Memory CAM mapping: ");
	for (i = 0; i < tlbcam_index - 1; i++)
		pr_cont("%lu/", tlbcam_sz(i) >> 20);
	pr_cont("%lu Mb, residual: %dMb\n", tlbcam_sz(tlbcam_index - 1) >> 20,
	        (unsigned int)((total_lowmem - __max_low_memory) >> 20));

	__initial_memory_limit_addr = memstart_addr + __max_low_memory;
}
