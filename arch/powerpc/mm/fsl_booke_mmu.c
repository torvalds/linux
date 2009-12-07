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

extern void loadcam_entry(unsigned int index);
unsigned int tlbcam_index;
static unsigned long cam[CONFIG_LOWMEM_CAM_NUM];

#define NUM_TLBCAMS	(16)

#if defined(CONFIG_LOWMEM_CAM_NUM_BOOL) && (CONFIG_LOWMEM_CAM_NUM >= NUM_TLBCAMS)
#error "LOWMEM_CAM_NUM must be less than NUM_TLBCAMS"
#endif

struct tlbcam TLBCAM[NUM_TLBCAMS];

struct tlbcamrange {
   	unsigned long start;
	unsigned long limit;
	phys_addr_t phys;
} tlbcam_addrs[NUM_TLBCAMS];

extern unsigned int tlbcam_index;

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

/*
 * Set up one of the I/D BAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 4 between 4k and 256M.
 */
void settlbcam(int index, unsigned long virt, phys_addr_t phys,
		unsigned int size, int flags, unsigned int pid)
{
	unsigned int tsize, lz;

	asm ("cntlzw %0,%1" : "=r" (lz) : "r" (size));
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

	TLBCAM[index].MAS3 = (phys & PAGE_MASK) | MAS3_SX | MAS3_SR;
	TLBCAM[index].MAS3 |= ((flags & _PAGE_RW) ? MAS3_SW : 0);

#ifndef CONFIG_KGDB /* want user access for breakpoints */
	if (flags & _PAGE_USER) {
	   TLBCAM[index].MAS3 |= MAS3_UX | MAS3_UR;
	   TLBCAM[index].MAS3 |= ((flags & _PAGE_RW) ? MAS3_UW : 0);
	}
#else
	TLBCAM[index].MAS3 |= MAS3_UX | MAS3_UR;
	TLBCAM[index].MAS3 |= ((flags & _PAGE_RW) ? MAS3_UW : 0);
#endif

	tlbcam_addrs[index].start = virt;
	tlbcam_addrs[index].limit = virt + size - 1;
	tlbcam_addrs[index].phys = phys;

	loadcam_entry(index);
}

void invalidate_tlbcam_entry(int index)
{
	TLBCAM[index].MAS0 = MAS0_TLBSEL(1) | MAS0_ESEL(index);
	TLBCAM[index].MAS1 = ~MAS1_VALID;

	loadcam_entry(index);
}

unsigned long __init mmu_mapin_ram(void)
{
	unsigned long virt = PAGE_OFFSET;
	phys_addr_t phys = memstart_addr;

	while (tlbcam_index < ARRAY_SIZE(cam) && cam[tlbcam_index]) {
		settlbcam(tlbcam_index, virt, phys, cam[tlbcam_index], PAGE_KERNEL_X, 0);
		virt += cam[tlbcam_index];
		phys += cam[tlbcam_index];
		tlbcam_index++;
	}

	return virt - PAGE_OFFSET;
}

/*
 * MMU_init_hw does the chip-specific initialization of the MMU hardware.
 */
void __init MMU_init_hw(void)
{
	flush_instruction_cache();
}

void __init
adjust_total_lowmem(void)
{
	phys_addr_t ram;
	unsigned int max_cam = (mfspr(SPRN_TLB1CFG) >> 16) & 0xff;
	char buf[ARRAY_SIZE(cam) * 5 + 1], *p = buf;
	int i;
	unsigned long virt = PAGE_OFFSET & 0xffffffffUL;
	unsigned long phys = memstart_addr & 0xffffffffUL;

	/* Convert (4^max) kB to (2^max) bytes */
	max_cam = max_cam * 2 + 10;

	/* adjust lowmem size to __max_low_memory */
	ram = min((phys_addr_t)__max_low_memory, (phys_addr_t)total_lowmem);

	/* Calculate CAM values */
	__max_low_memory = 0;
	for (i = 0; ram && i < ARRAY_SIZE(cam); i++) {
		unsigned int camsize = __ilog2(ram) & ~1U;
		unsigned int align = __ffs(virt | phys) & ~1U;

		if (camsize > align)
			camsize = align;
		if (camsize > max_cam)
			camsize = max_cam;

		cam[i] = 1UL << camsize;
		ram -= cam[i];
		__max_low_memory += cam[i];
		virt += cam[i];
		phys += cam[i];

		p += sprintf(p, "%lu/", cam[i] >> 20);
	}
	for (; i < ARRAY_SIZE(cam); i++)
		p += sprintf(p, "0/");
	p[-1] = '\0';

	pr_info("Memory CAM mapping: %s Mb, residual: %dMb\n", buf,
	        (unsigned int)((total_lowmem - __max_low_memory) >> 20));
	__initial_memory_limit_addr = memstart_addr + __max_low_memory;
}
