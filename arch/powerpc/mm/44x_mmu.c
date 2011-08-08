/*
 * Modifications by Matt Porter (mporter@mvista.com) to support
 * PPC44x Book E processors.
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

#include <linux/init.h>
#include <linux/memblock.h>

#include <asm/mmu.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/cacheflush.h>

#include "mmu_decl.h"

/* Used by the 44x TLB replacement exception handler.
 * Just needed it declared someplace.
 */
unsigned int tlb_44x_index; /* = 0 */
unsigned int tlb_44x_hwater = PPC44x_TLB_SIZE - 1 - PPC44x_EARLY_TLBS;
int icache_44x_need_flush;

unsigned long tlb_47x_boltmap[1024/8];

static void __cpuinit ppc44x_update_tlb_hwater(void)
{
	extern unsigned int tlb_44x_patch_hwater_D[];
	extern unsigned int tlb_44x_patch_hwater_I[];

	/* The TLB miss handlers hard codes the watermark in a cmpli
	 * instruction to improve performances rather than loading it
	 * from the global variable. Thus, we patch the instructions
	 * in the 2 TLB miss handlers when updating the value
	 */
	tlb_44x_patch_hwater_D[0] = (tlb_44x_patch_hwater_D[0] & 0xffff0000) |
		tlb_44x_hwater;
	flush_icache_range((unsigned long)&tlb_44x_patch_hwater_D[0],
			   (unsigned long)&tlb_44x_patch_hwater_D[1]);
	tlb_44x_patch_hwater_I[0] = (tlb_44x_patch_hwater_I[0] & 0xffff0000) |
		tlb_44x_hwater;
	flush_icache_range((unsigned long)&tlb_44x_patch_hwater_I[0],
			   (unsigned long)&tlb_44x_patch_hwater_I[1]);
}

/*
 * "Pins" a 256MB TLB entry in AS0 for kernel lowmem for 44x type MMU
 */
static void __init ppc44x_pin_tlb(unsigned int virt, unsigned int phys)
{
	unsigned int entry = tlb_44x_hwater--;

	ppc44x_update_tlb_hwater();

	mtspr(SPRN_MMUCR, 0);

	__asm__ __volatile__(
		"tlbwe	%2,%3,%4\n"
		"tlbwe	%1,%3,%5\n"
		"tlbwe	%0,%3,%6\n"
	:
#ifdef CONFIG_PPC47x
	: "r" (PPC47x_TLB2_S_RWX),
#else
	: "r" (PPC44x_TLB_SW | PPC44x_TLB_SR | PPC44x_TLB_SX | PPC44x_TLB_G),
#endif
	  "r" (phys),
	  "r" (virt | PPC44x_TLB_VALID | PPC44x_TLB_256M),
	  "r" (entry),
	  "i" (PPC44x_TLB_PAGEID),
	  "i" (PPC44x_TLB_XLAT),
	  "i" (PPC44x_TLB_ATTRIB));
}

static int __init ppc47x_find_free_bolted(void)
{
	unsigned int mmube0 = mfspr(SPRN_MMUBE0);
	unsigned int mmube1 = mfspr(SPRN_MMUBE1);

	if (!(mmube0 & MMUBE0_VBE0))
		return 0;
	if (!(mmube0 & MMUBE0_VBE1))
		return 1;
	if (!(mmube0 & MMUBE0_VBE2))
		return 2;
	if (!(mmube1 & MMUBE1_VBE3))
		return 3;
	if (!(mmube1 & MMUBE1_VBE4))
		return 4;
	if (!(mmube1 & MMUBE1_VBE5))
		return 5;
	return -1;
}

static void __init ppc47x_update_boltmap(void)
{
	unsigned int mmube0 = mfspr(SPRN_MMUBE0);
	unsigned int mmube1 = mfspr(SPRN_MMUBE1);

	if (mmube0 & MMUBE0_VBE0)
		__set_bit((mmube0 >> MMUBE0_IBE0_SHIFT) & 0xff,
			  tlb_47x_boltmap);
	if (mmube0 & MMUBE0_VBE1)
		__set_bit((mmube0 >> MMUBE0_IBE1_SHIFT) & 0xff,
			  tlb_47x_boltmap);
	if (mmube0 & MMUBE0_VBE2)
		__set_bit((mmube0 >> MMUBE0_IBE2_SHIFT) & 0xff,
			  tlb_47x_boltmap);
	if (mmube1 & MMUBE1_VBE3)
		__set_bit((mmube1 >> MMUBE1_IBE3_SHIFT) & 0xff,
			  tlb_47x_boltmap);
	if (mmube1 & MMUBE1_VBE4)
		__set_bit((mmube1 >> MMUBE1_IBE4_SHIFT) & 0xff,
			  tlb_47x_boltmap);
	if (mmube1 & MMUBE1_VBE5)
		__set_bit((mmube1 >> MMUBE1_IBE5_SHIFT) & 0xff,
			  tlb_47x_boltmap);
}

/*
 * "Pins" a 256MB TLB entry in AS0 for kernel lowmem for 47x type MMU
 */
static void __cpuinit ppc47x_pin_tlb(unsigned int virt, unsigned int phys)
{
	unsigned int rA;
	int bolted;

	/* Base rA is HW way select, way 0, bolted bit set */
	rA = 0x88000000;

	/* Look for a bolted entry slot */
	bolted = ppc47x_find_free_bolted();
	BUG_ON(bolted < 0);

	/* Insert bolted slot number */
	rA |= bolted << 24;

	pr_debug("256M TLB entry for 0x%08x->0x%08x in bolt slot %d\n",
		 virt, phys, bolted);

	mtspr(SPRN_MMUCR, 0);

	__asm__ __volatile__(
		"tlbwe	%2,%3,0\n"
		"tlbwe	%1,%3,1\n"
		"tlbwe	%0,%3,2\n"
		:
		: "r" (PPC47x_TLB2_SW | PPC47x_TLB2_SR |
		       PPC47x_TLB2_SX
#ifdef CONFIG_SMP
		       | PPC47x_TLB2_M
#endif
		       ),
		  "r" (phys),
		  "r" (virt | PPC47x_TLB0_VALID | PPC47x_TLB0_256M),
		  "r" (rA));
}

void __init MMU_init_hw(void)
{
	/* This is not useful on 47x but won't hurt either */
	ppc44x_update_tlb_hwater();

	flush_instruction_cache();
}

unsigned long __init mmu_mapin_ram(unsigned long top)
{
	unsigned long addr;
	unsigned long memstart = memstart_addr & ~(PPC_PIN_SIZE - 1);

	/* Pin in enough TLBs to cover any lowmem not covered by the
	 * initial 256M mapping established in head_44x.S */
	for (addr = memstart + PPC_PIN_SIZE; addr < lowmem_end_addr;
	     addr += PPC_PIN_SIZE) {
		if (mmu_has_feature(MMU_FTR_TYPE_47x))
			ppc47x_pin_tlb(addr + PAGE_OFFSET, addr);
		else
			ppc44x_pin_tlb(addr + PAGE_OFFSET, addr);
	}
	if (mmu_has_feature(MMU_FTR_TYPE_47x)) {
		ppc47x_update_boltmap();

#ifdef DEBUG
		{
			int i;

			printk(KERN_DEBUG "bolted entries: ");
			for (i = 0; i < 255; i++) {
				if (test_bit(i, tlb_47x_boltmap))
					printk("%d ", i);
			}
			printk("\n");
		}
#endif /* DEBUG */
	}
	return total_lowmem;
}

void setup_initial_memory_limit(phys_addr_t first_memblock_base,
				phys_addr_t first_memblock_size)
{
	u64 size;

#ifndef CONFIG_RELOCATABLE
	/* We don't currently support the first MEMBLOCK not mapping 0
	 * physical on those processors
	 */
	BUG_ON(first_memblock_base != 0);
#endif

	/* 44x has a 256M TLB entry pinned at boot */
	size = (min_t(u64, first_memblock_size, PPC_PIN_SIZE));
	memblock_set_current_limit(first_memblock_base + size);
}

#ifdef CONFIG_SMP
void __cpuinit mmu_init_secondary(int cpu)
{
	unsigned long addr;
	unsigned long memstart = memstart_addr & ~(PPC_PIN_SIZE - 1);

	/* Pin in enough TLBs to cover any lowmem not covered by the
	 * initial 256M mapping established in head_44x.S
	 *
	 * WARNING: This is called with only the first 256M of the
	 * linear mapping in the TLB and we can't take faults yet
	 * so beware of what this code uses. It runs off a temporary
	 * stack. current (r2) isn't initialized, smp_processor_id()
	 * will not work, current thread info isn't accessible, ...
	 */
	for (addr = memstart + PPC_PIN_SIZE; addr < lowmem_end_addr;
	     addr += PPC_PIN_SIZE) {
		if (mmu_has_feature(MMU_FTR_TYPE_47x))
			ppc47x_pin_tlb(addr + PAGE_OFFSET, addr);
		else
			ppc44x_pin_tlb(addr + PAGE_OFFSET, addr);
	}
}
#endif /* CONFIG_SMP */
