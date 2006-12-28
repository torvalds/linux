/*
 * arch/sh/mm/tlb-sh4.c
 *
 * SH-4 specific TLB operations
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

	/*
	 * NOTE: PTEH.ASID should be set to this MM
	 *       _AND_ we need to write ASID to the array.
	 *
	 * It would be simple if we didn't need to set PTEH.ASID...
	 */
	addr = MMU_UTLB_ADDRESS_ARRAY | MMU_PAGE_ASSOC_BIT;
	data = page | asid; /* VALID bit is off */
	jump_to_P2();
	ctrl_outl(data, addr);
	back_to_P1();
}
