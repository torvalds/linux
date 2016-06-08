/*
 * Dump R3000 TLB for debugging purposes.
 *
 * Copyright (C) 1994, 1995 by Waldorf Electronics, written by Ralf Baechle.
 * Copyright (C) 1999 by Silicon Graphics, Inc.
 * Copyright (C) 1999 by Harald Koerfgen
 */
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/mipsregs.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlbdebug.h>

extern int r3k_have_wired_reg;

void dump_tlb_regs(void)
{
	pr_info("Index    : %0x\n", read_c0_index());
	pr_info("EntryHi  : %0lx\n", read_c0_entryhi());
	pr_info("EntryLo  : %0lx\n", read_c0_entrylo0());
	if (r3k_have_wired_reg)
		pr_info("Wired    : %0x\n", read_c0_wired());
}

static void dump_tlb(int first, int last)
{
	int	i;
	unsigned int asid;
	unsigned long entryhi, entrylo0, asid_mask;

	asid_mask = cpu_asid_mask(&current_cpu_data);
	asid = read_c0_entryhi() & asid_mask;

	for (i = first; i <= last; i++) {
		write_c0_index(i<<8);
		__asm__ __volatile__(
			".set\tnoreorder\n\t"
			"tlbr\n\t"
			"nop\n\t"
			".set\treorder");
		entryhi	 = read_c0_entryhi();
		entrylo0 = read_c0_entrylo0();

		/* Unused entries have a virtual address of KSEG0.  */
		if ((entryhi & PAGE_MASK) != KSEG0 &&
		    (entrylo0 & R3K_ENTRYLO_G ||
		     (entryhi & asid_mask) == asid)) {
			/*
			 * Only print entries in use
			 */
			printk("Index: %2d ", i);

			printk("va=%08lx asid=%08lx"
			       "  [pa=%06lx n=%d d=%d v=%d g=%d]",
			       entryhi & PAGE_MASK,
			       entryhi & asid_mask,
			       entrylo0 & PAGE_MASK,
			       (entrylo0 & R3K_ENTRYLO_N) ? 1 : 0,
			       (entrylo0 & R3K_ENTRYLO_D) ? 1 : 0,
			       (entrylo0 & R3K_ENTRYLO_V) ? 1 : 0,
			       (entrylo0 & R3K_ENTRYLO_G) ? 1 : 0);
		}
	}
	printk("\n");

	write_c0_entryhi(asid);
}

void dump_tlb_all(void)
{
	dump_tlb(0, current_cpu_data.tlbsize - 1);
}
