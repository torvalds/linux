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
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
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
#include <asm/mmu.h>
#include <asm/system.h>
#include <asm/page.h>

#include "mmu_decl.h"

/* Used by the 44x TLB replacement exception handler.
 * Just needed it declared someplace.
 */
unsigned int tlb_44x_index; /* = 0 */
unsigned int tlb_44x_hwater = PPC44x_TLB_SIZE - 1 - PPC44x_EARLY_TLBS;

/*
 * "Pins" a 256MB TLB entry in AS0 for kernel lowmem
 */
static void __init ppc44x_pin_tlb(unsigned int virt, unsigned int phys)
{
	__asm__ __volatile__(
		"tlbwe	%2,%3,%4\n"
		"tlbwe	%1,%3,%5\n"
		"tlbwe	%0,%3,%6\n"
	:
	: "r" (PPC44x_TLB_SW | PPC44x_TLB_SR | PPC44x_TLB_SX | PPC44x_TLB_G),
	  "r" (phys),
	  "r" (virt | PPC44x_TLB_VALID | PPC44x_TLB_256M),
	  "r" (tlb_44x_hwater--), /* slot for this TLB entry */
	  "i" (PPC44x_TLB_PAGEID),
	  "i" (PPC44x_TLB_XLAT),
	  "i" (PPC44x_TLB_ATTRIB));
}

void __init MMU_init_hw(void)
{
	flush_instruction_cache();
}

unsigned long __init mmu_mapin_ram(void)
{
	unsigned long addr;

	/* Pin in enough TLBs to cover any lowmem not covered by the
	 * initial 256M mapping established in head_44x.S */
	for (addr = PPC_PIN_SIZE; addr < total_lowmem;
	     addr += PPC_PIN_SIZE)
		ppc44x_pin_tlb(addr + PAGE_OFFSET, addr);

	return total_lowmem;
}
