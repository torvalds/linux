// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1994, 1995 by Waldorf Electronics, written by Ralf Baechle.
 * Copyright (C) 1999 by Silicon Graphics, Inc.
 */
#include <linux/kernel.h>
#include <linux/mm.h>

#include <asm/loongarch.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

void dump_tlb_regs(void)
{
	const int field = 2 * sizeof(unsigned long);

	pr_info("Index    : %0x\n", read_csr_tlbidx());
	pr_info("PageSize : %0x\n", read_csr_pagesize());
	pr_info("EntryHi  : %0*llx\n", field, read_csr_entryhi());
	pr_info("EntryLo0 : %0*llx\n", field, read_csr_entrylo0());
	pr_info("EntryLo1 : %0*llx\n", field, read_csr_entrylo1());
}

static void dump_tlb(int first, int last)
{
	unsigned long s_entryhi, entryhi, asid;
	unsigned long long entrylo0, entrylo1, pa;
	unsigned int index;
	unsigned int s_index, s_asid;
	unsigned int pagesize, c0, c1, i;
	unsigned long asidmask = cpu_asid_mask(&current_cpu_data);
	int pwidth = 11;
	int vwidth = 11;
	int asidwidth = DIV_ROUND_UP(ilog2(asidmask) + 1, 4);

	s_entryhi = read_csr_entryhi();
	s_index = read_csr_tlbidx();
	s_asid = read_csr_asid();

	for (i = first; i <= last; i++) {
		write_csr_index(i);
		tlb_read();
		pagesize = read_csr_pagesize();
		entryhi	 = read_csr_entryhi();
		entrylo0 = read_csr_entrylo0();
		entrylo1 = read_csr_entrylo1();
		index = read_csr_tlbidx();
		asid = read_csr_asid();

		/* EHINV bit marks entire entry as invalid */
		if (index & CSR_TLBIDX_EHINV)
			continue;
		/*
		 * ASID takes effect in absence of G (global) bit.
		 */
		if (!((entrylo0 | entrylo1) & ENTRYLO_G) &&
		    asid != s_asid)
			continue;

		/*
		 * Only print entries in use
		 */
		pr_info("Index: %2d pgsize=%x ", i, (1 << pagesize));

		c0 = (entrylo0 & ENTRYLO_C) >> ENTRYLO_C_SHIFT;
		c1 = (entrylo1 & ENTRYLO_C) >> ENTRYLO_C_SHIFT;

		pr_cont("va=%0*lx asid=%0*lx",
			vwidth, (entryhi & ~0x1fffUL), asidwidth, asid & asidmask);

		/* NR/NX are in awkward places, so mask them off separately */
		pa = entrylo0 & ~(ENTRYLO_NR | ENTRYLO_NX);
		pa = pa & PAGE_MASK;
		pr_cont("\n\t[");
		pr_cont("ri=%d xi=%d ",
			(entrylo0 & ENTRYLO_NR) ? 1 : 0,
			(entrylo0 & ENTRYLO_NX) ? 1 : 0);
		pr_cont("pa=%0*llx c=%d d=%d v=%d g=%d plv=%lld] [",
			pwidth, pa, c0,
			(entrylo0 & ENTRYLO_D) ? 1 : 0,
			(entrylo0 & ENTRYLO_V) ? 1 : 0,
			(entrylo0 & ENTRYLO_G) ? 1 : 0,
			(entrylo0 & ENTRYLO_PLV) >> ENTRYLO_PLV_SHIFT);
		/* NR/NX are in awkward places, so mask them off separately */
		pa = entrylo1 & ~(ENTRYLO_NR | ENTRYLO_NX);
		pa = pa & PAGE_MASK;
		pr_cont("ri=%d xi=%d ",
			(entrylo1 & ENTRYLO_NR) ? 1 : 0,
			(entrylo1 & ENTRYLO_NX) ? 1 : 0);
		pr_cont("pa=%0*llx c=%d d=%d v=%d g=%d plv=%lld]\n",
			pwidth, pa, c1,
			(entrylo1 & ENTRYLO_D) ? 1 : 0,
			(entrylo1 & ENTRYLO_V) ? 1 : 0,
			(entrylo1 & ENTRYLO_G) ? 1 : 0,
			(entrylo1 & ENTRYLO_PLV) >> ENTRYLO_PLV_SHIFT);
	}
	pr_info("\n");

	write_csr_entryhi(s_entryhi);
	write_csr_tlbidx(s_index);
	write_csr_asid(s_asid);
}

void dump_tlb_all(void)
{
	dump_tlb(0, current_cpu_data.tlbsize - 1);
}
