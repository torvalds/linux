// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016, Rashmica Gupta, IBM Corp.
 *
 * This traverses the kernel pagetables and dumps the
 * information about the used sections of memory to
 * /sys/kernel/debug/kernel_pagetables.
 *
 * Derived from the arm64 implementation:
 * Copyright (c) 2014, The Linux Foundation, Laura Abbott.
 * (C) Copyright 2008 Intel Corporation, Arjan van de Ven.
 */
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/hugetlb.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/fixmap.h>
#include <linux/const.h>
#include <asm/page.h>
#include <asm/hugetlb.h>

#include <mm/mmu_decl.h>

#include "ptdump.h"

/*
 * To visualise what is happening,
 *
 *  - PTRS_PER_P** = how many entries there are in the corresponding P**
 *  - P**_SHIFT = how many bits of the address we use to index into the
 * corresponding P**
 *  - P**_SIZE is how much memory we can access through the table - not the
 * size of the table itself.
 * P**={PGD, PUD, PMD, PTE}
 *
 *
 * Each entry of the PGD points to a PUD. Each entry of a PUD points to a
 * PMD. Each entry of a PMD points to a PTE. And every PTE entry points to
 * a page.
 *
 * In the case where there are only 3 levels, the PUD is folded into the
 * PGD: every PUD has only one entry which points to the PMD.
 *
 * The page dumper groups page table entries of the same type into a single
 * description. It uses pg_state to track the range information while
 * iterating over the PTE entries. When the continuity is broken it then
 * dumps out a description of the range - ie PTEs that are virtually contiguous
 * with the same PTE flags are chunked together. This is to make it clear how
 * different areas of the kernel virtual memory are used.
 *
 */
struct pg_state {
	struct seq_file *seq;
	const struct addr_marker *marker;
	unsigned long start_address;
	unsigned long start_pa;
	unsigned int level;
	u64 current_flags;
	bool check_wx;
	unsigned long wx_pages;
};

struct addr_marker {
	unsigned long start_address;
	const char *name;
};

static struct addr_marker address_markers[] = {
	{ 0,	"Start of kernel VM" },
#ifdef MODULES_VADDR
	{ 0,	"modules start" },
	{ 0,	"modules end" },
#endif
	{ 0,	"vmalloc() Area" },
	{ 0,	"vmalloc() End" },
#ifdef CONFIG_PPC64
	{ 0,	"isa I/O start" },
	{ 0,	"isa I/O end" },
	{ 0,	"phb I/O start" },
	{ 0,	"phb I/O end" },
	{ 0,	"I/O remap start" },
	{ 0,	"I/O remap end" },
	{ 0,	"vmemmap start" },
#else
	{ 0,	"Early I/O remap start" },
	{ 0,	"Early I/O remap end" },
#ifdef CONFIG_HIGHMEM
	{ 0,	"Highmem PTEs start" },
	{ 0,	"Highmem PTEs end" },
#endif
	{ 0,	"Fixmap start" },
	{ 0,	"Fixmap end" },
#endif
#ifdef CONFIG_KASAN
	{ 0,	"kasan shadow mem start" },
	{ 0,	"kasan shadow mem end" },
#endif
	{ -1,	NULL },
};

#define pt_dump_seq_printf(m, fmt, args...)	\
({						\
	if (m)					\
		seq_printf(m, fmt, ##args);	\
})

#define pt_dump_seq_putc(m, c)		\
({					\
	if (m)				\
		seq_putc(m, c);		\
})

void pt_dump_size(struct seq_file *m, unsigned long size)
{
	static const char units[] = "KMGTPE";
	const char *unit = units;

	/* Work out what appropriate unit to use */
	while (!(size & 1023) && unit[1]) {
		size >>= 10;
		unit++;
	}
	pt_dump_seq_printf(m, "%9lu%c ", size, *unit);
}

static void dump_flag_info(struct pg_state *st, const struct flag_info
		*flag, u64 pte, int num)
{
	unsigned int i;

	for (i = 0; i < num; i++, flag++) {
		const char *s = NULL;
		u64 val;

		/* flag not defined so don't check it */
		if (flag->mask == 0)
			continue;
		/* Some 'flags' are actually values */
		if (flag->is_val) {
			val = pte & flag->val;
			if (flag->shift)
				val = val >> flag->shift;
			pt_dump_seq_printf(st->seq, "  %s:%llx", flag->set, val);
		} else {
			if ((pte & flag->mask) == flag->val)
				s = flag->set;
			else
				s = flag->clear;
			if (s)
				pt_dump_seq_printf(st->seq, "  %s", s);
		}
		st->current_flags &= ~flag->mask;
	}
	if (st->current_flags != 0)
		pt_dump_seq_printf(st->seq, "  unknown flags:%llx", st->current_flags);
}

static void dump_addr(struct pg_state *st, unsigned long addr)
{
#ifdef CONFIG_PPC64
#define REG		"0x%016lx"
#else
#define REG		"0x%08lx"
#endif

	pt_dump_seq_printf(st->seq, REG "-" REG " ", st->start_address, addr - 1);
	pt_dump_seq_printf(st->seq, " " REG " ", st->start_pa);
	pt_dump_size(st->seq, (addr - st->start_address) >> 10);
}

static void note_prot_wx(struct pg_state *st, unsigned long addr)
{
	pte_t pte = __pte(st->current_flags);

	if (!IS_ENABLED(CONFIG_PPC_DEBUG_WX) || !st->check_wx)
		return;

	if (!pte_write(pte) || !pte_exec(pte))
		return;

	WARN_ONCE(1, "powerpc/mm: Found insecure W+X mapping at address %p/%pS\n",
		  (void *)st->start_address, (void *)st->start_address);

	st->wx_pages += (addr - st->start_address) / PAGE_SIZE;
}

static void note_page_update_state(struct pg_state *st, unsigned long addr,
				   unsigned int level, u64 val, unsigned long page_size)
{
	u64 flag = val & pg_level[level].mask;
	u64 pa = val & PTE_RPN_MASK;

	st->level = level;
	st->current_flags = flag;
	st->start_address = addr;
	st->start_pa = pa;

	while (addr >= st->marker[1].start_address) {
		st->marker++;
		pt_dump_seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
	}
}

static void note_page(struct pg_state *st, unsigned long addr,
	       unsigned int level, u64 val, unsigned long page_size)
{
	u64 flag = val & pg_level[level].mask;

	/* At first no level is set */
	if (!st->level) {
		pt_dump_seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
		note_page_update_state(st, addr, level, val, page_size);
	/*
	 * Dump the section of virtual memory when:
	 *   - the PTE flags from one entry to the next differs.
	 *   - we change levels in the tree.
	 *   - the address is in a different section of memory and is thus
	 *   used for a different purpose, regardless of the flags.
	 */
	} else if (flag != st->current_flags || level != st->level ||
		   addr >= st->marker[1].start_address) {

		/* Check the PTE flags */
		if (st->current_flags) {
			note_prot_wx(st, addr);
			dump_addr(st, addr);

			/* Dump all the flags */
			if (pg_level[st->level].flag)
				dump_flag_info(st, pg_level[st->level].flag,
					  st->current_flags,
					  pg_level[st->level].num);

			pt_dump_seq_putc(st->seq, '\n');
		}

		/*
		 * Address indicates we have passed the end of the
		 * current section of virtual memory
		 */
		note_page_update_state(st, addr, level, val, page_size);
	}
}

static void walk_pte(struct pg_state *st, pmd_t *pmd, unsigned long start)
{
	pte_t *pte = pte_offset_kernel(pmd, 0);
	unsigned long addr;
	unsigned int i;

	for (i = 0; i < PTRS_PER_PTE; i++, pte++) {
		addr = start + i * PAGE_SIZE;
		note_page(st, addr, 4, pte_val(*pte), PAGE_SIZE);

	}
}

static void walk_hugepd(struct pg_state *st, hugepd_t *phpd, unsigned long start,
			int pdshift, int level)
{
#ifdef CONFIG_ARCH_HAS_HUGEPD
	unsigned int i;
	int shift = hugepd_shift(*phpd);
	int ptrs_per_hpd = pdshift - shift > 0 ? 1 << (pdshift - shift) : 1;

	if (start & ((1 << shift) - 1))
		return;

	for (i = 0; i < ptrs_per_hpd; i++) {
		unsigned long addr = start + (i << shift);
		pte_t *pte = hugepte_offset(*phpd, addr, pdshift);

		note_page(st, addr, level + 1, pte_val(*pte), 1 << shift);
	}
#endif
}

static void walk_pmd(struct pg_state *st, pud_t *pud, unsigned long start)
{
	pmd_t *pmd = pmd_offset(pud, 0);
	unsigned long addr;
	unsigned int i;

	for (i = 0; i < PTRS_PER_PMD; i++, pmd++) {
		addr = start + i * PMD_SIZE;
		if (!pmd_none(*pmd) && !pmd_is_leaf(*pmd))
			/* pmd exists */
			walk_pte(st, pmd, addr);
		else
			note_page(st, addr, 3, pmd_val(*pmd), PMD_SIZE);
	}
}

static void walk_pud(struct pg_state *st, p4d_t *p4d, unsigned long start)
{
	pud_t *pud = pud_offset(p4d, 0);
	unsigned long addr;
	unsigned int i;

	for (i = 0; i < PTRS_PER_PUD; i++, pud++) {
		addr = start + i * PUD_SIZE;
		if (!pud_none(*pud) && !pud_is_leaf(*pud))
			/* pud exists */
			walk_pmd(st, pud, addr);
		else
			note_page(st, addr, 2, pud_val(*pud), PUD_SIZE);
	}
}

static void walk_pagetables(struct pg_state *st)
{
	unsigned int i;
	unsigned long addr = st->start_address & PGDIR_MASK;
	pgd_t *pgd = pgd_offset_k(addr);

	/*
	 * Traverse the linux pagetable structure and dump pages that are in
	 * the hash pagetable.
	 */
	for (i = pgd_index(addr); i < PTRS_PER_PGD; i++, pgd++, addr += PGDIR_SIZE) {
		p4d_t *p4d = p4d_offset(pgd, 0);

		if (p4d_none(*p4d) || p4d_is_leaf(*p4d))
			note_page(st, addr, 1, p4d_val(*p4d), PGDIR_SIZE);
		else if (is_hugepd(__hugepd(p4d_val(*p4d))))
			walk_hugepd(st, (hugepd_t *)p4d, addr, PGDIR_SHIFT, 1);
		else
			/* p4d exists */
			walk_pud(st, p4d, addr);
	}
}

static void populate_markers(void)
{
	int i = 0;

#ifdef CONFIG_PPC64
	address_markers[i++].start_address = PAGE_OFFSET;
#else
	address_markers[i++].start_address = TASK_SIZE;
#endif
#ifdef MODULES_VADDR
	address_markers[i++].start_address = MODULES_VADDR;
	address_markers[i++].start_address = MODULES_END;
#endif
	address_markers[i++].start_address = VMALLOC_START;
	address_markers[i++].start_address = VMALLOC_END;
#ifdef CONFIG_PPC64
	address_markers[i++].start_address = ISA_IO_BASE;
	address_markers[i++].start_address = ISA_IO_END;
	address_markers[i++].start_address = PHB_IO_BASE;
	address_markers[i++].start_address = PHB_IO_END;
	address_markers[i++].start_address = IOREMAP_BASE;
	address_markers[i++].start_address = IOREMAP_END;
	/* What is the ifdef about? */
#ifdef CONFIG_PPC_BOOK3S_64
	address_markers[i++].start_address =  H_VMEMMAP_START;
#else
	address_markers[i++].start_address =  VMEMMAP_BASE;
#endif
#else /* !CONFIG_PPC64 */
	address_markers[i++].start_address = ioremap_bot;
	address_markers[i++].start_address = IOREMAP_TOP;
#ifdef CONFIG_HIGHMEM
	address_markers[i++].start_address = PKMAP_BASE;
	address_markers[i++].start_address = PKMAP_ADDR(LAST_PKMAP);
#endif
	address_markers[i++].start_address = FIXADDR_START;
	address_markers[i++].start_address = FIXADDR_TOP;
#ifdef CONFIG_KASAN
	address_markers[i++].start_address = KASAN_SHADOW_START;
	address_markers[i++].start_address = KASAN_SHADOW_END;
#endif
#endif /* CONFIG_PPC64 */
}

static int ptdump_show(struct seq_file *m, void *v)
{
	struct pg_state st = {
		.seq = m,
		.marker = address_markers,
		.start_address = IS_ENABLED(CONFIG_PPC64) ? PAGE_OFFSET : TASK_SIZE,
	};

#ifdef CONFIG_PPC64
	if (!radix_enabled())
		st.start_address = KERN_VIRT_START;
#endif

	/* Traverse kernel page tables */
	walk_pagetables(&st);
	note_page(&st, 0, 0, 0, 0);
	return 0;
}


static int ptdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ptdump_show, NULL);
}

static const struct file_operations ptdump_fops = {
	.open		= ptdump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void build_pgtable_complete_mask(void)
{
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(pg_level); i++)
		if (pg_level[i].flag)
			for (j = 0; j < pg_level[i].num; j++)
				pg_level[i].mask |= pg_level[i].flag[j].mask;
}

#ifdef CONFIG_PPC_DEBUG_WX
void ptdump_check_wx(void)
{
	struct pg_state st = {
		.seq = NULL,
		.marker = address_markers,
		.check_wx = true,
		.start_address = IS_ENABLED(CONFIG_PPC64) ? PAGE_OFFSET : TASK_SIZE,
	};

#ifdef CONFIG_PPC64
	if (!radix_enabled())
		st.start_address = KERN_VIRT_START;
#endif

	walk_pagetables(&st);

	if (st.wx_pages)
		pr_warn("Checked W+X mappings: FAILED, %lu W+X pages found\n",
			st.wx_pages);
	else
		pr_info("Checked W+X mappings: passed, no W+X pages found\n");
}
#endif

static int ptdump_init(void)
{
	populate_markers();
	build_pgtable_complete_mask();
	debugfs_create_file("kernel_page_tables", 0400, NULL, NULL,
			    &ptdump_fops);
	return 0;
}
device_initcall(ptdump_init);
