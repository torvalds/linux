/*
 * arch/sh/mm/tlb-sh3.c
 *
 * SH-3 specific TLB operations
 *
 * Copyright (C) 1999  Niibe Yutaka
 * Copyright (C) 2002  Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/io.h>
#include <asm/system.h>
#include <asm/mmu_context.h>

void __flush_tlb_page(unsigned long asid, unsigned long page)
{
	unsigned long addr, data;
	int i, ways = MMU_NTLB_WAYS;

	/*
	 * NOTE: PTEH.ASID should be set to this MM
	 *       _AND_ we need to write ASID to the array.
	 *
	 * It would be simple if we didn't need to set PTEH.ASID...
	 */
	addr = MMU_TLB_ADDRESS_ARRAY | (page & 0x1F000);
	data = (page & 0xfffe0000) | asid; /* VALID bit is off */

	if ((current_cpu_data.flags & CPU_HAS_MMU_PAGE_ASSOC)) {
		addr |= MMU_PAGE_ASSOC_BIT;
		ways = 1;	/* we already know the way .. */
	}

	for (i = 0; i < ways; i++)
		ctrl_outl(data, addr + (i << 8));
}
