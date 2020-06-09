// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 SiFive
 */

#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/ptdump.h>

#include <asm/ptdump.h>
#include <linux/pgtable.h>
#include <asm/kasan.h>

#define pt_dump_seq_printf(m, fmt, args...)	\
({						\
	if (m)					\
		seq_printf(m, fmt, ##args);	\
})

#define pt_dump_seq_puts(m, fmt)	\
({					\
	if (m)				\
		seq_printf(m, fmt);	\
})

/*
 * The page dumper groups page table entries of the same type into a single
 * description. It uses pg_state to track the range information while
 * iterating over the pte entries. When the continuity is broken it then
 * dumps out a description of the range.
 */
struct pg_state {
	struct ptdump_state ptdump;
	struct seq_file *seq;
	const struct addr_marker *marker;
	unsigned long start_address;
	unsigned long start_pa;
	unsigned long last_pa;
	int level;
	u64 current_prot;
	bool check_wx;
	unsigned long wx_pages;
};

/* Address marker */
struct addr_marker {
	unsigned long start_address;
	const char *name;
};

static struct addr_marker address_markers[] = {
#ifdef CONFIG_KASAN
	{KASAN_SHADOW_START,	"Kasan shadow start"},
	{KASAN_SHADOW_END,	"Kasan shadow end"},
#endif
	{FIXADDR_START,		"Fixmap start"},
	{FIXADDR_TOP,		"Fixmap end"},
	{PCI_IO_START,		"PCI I/O start"},
	{PCI_IO_END,		"PCI I/O end"},
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	{VMEMMAP_START,		"vmemmap start"},
	{VMEMMAP_END,		"vmemmap end"},
#endif
	{VMALLOC_START,		"vmalloc() area"},
	{VMALLOC_END,		"vmalloc() end"},
	{PAGE_OFFSET,		"Linear mapping"},
	{-1, NULL},
};

/* Page Table Entry */
struct prot_bits {
	u64 mask;
	u64 val;
	const char *set;
	const char *clear;
};

static const struct prot_bits pte_bits[] = {
	{
		.mask = _PAGE_SOFT,
		.val = _PAGE_SOFT,
		.set = "RSW",
		.clear = "   ",
	}, {
		.mask = _PAGE_DIRTY,
		.val = _PAGE_DIRTY,
		.set = "D",
		.clear = ".",
	}, {
		.mask = _PAGE_ACCESSED,
		.val = _PAGE_ACCESSED,
		.set = "A",
		.clear = ".",
	}, {
		.mask = _PAGE_GLOBAL,
		.val = _PAGE_GLOBAL,
		.set = "G",
		.clear = ".",
	}, {
		.mask = _PAGE_USER,
		.val = _PAGE_USER,
		.set = "U",
		.clear = ".",
	}, {
		.mask = _PAGE_EXEC,
		.val = _PAGE_EXEC,
		.set = "X",
		.clear = ".",
	}, {
		.mask = _PAGE_WRITE,
		.val = _PAGE_WRITE,
		.set = "W",
		.clear = ".",
	}, {
		.mask = _PAGE_READ,
		.val = _PAGE_READ,
		.set = "R",
		.clear = ".",
	}, {
		.mask = _PAGE_PRESENT,
		.val = _PAGE_PRESENT,
		.set = "V",
		.clear = ".",
	}
};

/* Page Level */
struct pg_level {
	const char *name;
	u64 mask;
};

static struct pg_level pg_level[] = {
	{ /* pgd */
		.name = "PGD",
	}, { /* p4d */
		.name = (CONFIG_PGTABLE_LEVELS > 4) ? "P4D" : "PGD",
	}, { /* pud */
		.name = (CONFIG_PGTABLE_LEVELS > 3) ? "PUD" : "PGD",
	}, { /* pmd */
		.name = (CONFIG_PGTABLE_LEVELS > 2) ? "PMD" : "PGD",
	}, { /* pte */
		.name = "PTE",
	},
};

static void dump_prot(struct pg_state *st)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pte_bits); i++) {
		const char *s;

		if ((st->current_prot & pte_bits[i].mask) == pte_bits[i].val)
			s = pte_bits[i].set;
		else
			s = pte_bits[i].clear;

		if (s)
			pt_dump_seq_printf(st->seq, " %s", s);
	}
}

#ifdef CONFIG_64BIT
#define ADDR_FORMAT	"0x%016lx"
#else
#define ADDR_FORMAT	"0x%08lx"
#endif
static void dump_addr(struct pg_state *st, unsigned long addr)
{
	static const char units[] = "KMGTPE";
	const char *unit = units;
	unsigned long delta;

	pt_dump_seq_printf(st->seq, ADDR_FORMAT "-" ADDR_FORMAT "   ",
			   st->start_address, addr);

	pt_dump_seq_printf(st->seq, " " ADDR_FORMAT " ", st->start_pa);
	delta = (addr - st->start_address) >> 10;

	while (!(delta & 1023) && unit[1]) {
		delta >>= 10;
		unit++;
	}

	pt_dump_seq_printf(st->seq, "%9lu%c %s", delta, *unit,
			   pg_level[st->level].name);
}

static void note_prot_wx(struct pg_state *st, unsigned long addr)
{
	if (!st->check_wx)
		return;

	if ((st->current_prot & (_PAGE_WRITE | _PAGE_EXEC)) !=
	    (_PAGE_WRITE | _PAGE_EXEC))
		return;

	WARN_ONCE(1, "riscv/mm: Found insecure W+X mapping at address %p/%pS\n",
		  (void *)st->start_address, (void *)st->start_address);

	st->wx_pages += (addr - st->start_address) / PAGE_SIZE;
}

static void note_page(struct ptdump_state *pt_st, unsigned long addr,
		      int level, u64 val)
{
	struct pg_state *st = container_of(pt_st, struct pg_state, ptdump);
	u64 pa = PFN_PHYS(pte_pfn(__pte(val)));
	u64 prot = 0;

	if (level >= 0)
		prot = val & pg_level[level].mask;

	if (st->level == -1) {
		st->level = level;
		st->current_prot = prot;
		st->start_address = addr;
		st->start_pa = pa;
		st->last_pa = pa;
		pt_dump_seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
	} else if (prot != st->current_prot ||
		   level != st->level || addr >= st->marker[1].start_address) {
		if (st->current_prot) {
			note_prot_wx(st, addr);
			dump_addr(st, addr);
			dump_prot(st);
			pt_dump_seq_puts(st->seq, "\n");
		}

		while (addr >= st->marker[1].start_address) {
			st->marker++;
			pt_dump_seq_printf(st->seq, "---[ %s ]---\n",
					   st->marker->name);
		}

		st->start_address = addr;
		st->start_pa = pa;
		st->last_pa = pa;
		st->current_prot = prot;
		st->level = level;
	} else {
		st->last_pa = pa;
	}
}

static void ptdump_walk(struct seq_file *s)
{
	struct pg_state st = {
		.seq = s,
		.marker = address_markers,
		.level = -1,
		.ptdump = {
			.note_page = note_page,
			.range = (struct ptdump_range[]) {
				{KERN_VIRT_START, ULONG_MAX},
				{0, 0}
			}
		}
	};

	ptdump_walk_pgd(&st.ptdump, &init_mm, NULL);
}

void ptdump_check_wx(void)
{
	struct pg_state st = {
		.seq = NULL,
		.marker = (struct addr_marker[]) {
			{0, NULL},
			{-1, NULL},
		},
		.level = -1,
		.check_wx = true,
		.ptdump = {
			.note_page = note_page,
			.range = (struct ptdump_range[]) {
				{KERN_VIRT_START, ULONG_MAX},
				{0, 0}
			}
		}
	};

	ptdump_walk_pgd(&st.ptdump, &init_mm, NULL);

	if (st.wx_pages)
		pr_warn("Checked W+X mappings: failed, %lu W+X pages found\n",
			st.wx_pages);
	else
		pr_info("Checked W+X mappings: passed, no W+X pages found\n");
}

static int ptdump_show(struct seq_file *m, void *v)
{
	ptdump_walk(m);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ptdump);

static int ptdump_init(void)
{
	unsigned int i, j;

	for (i = 0; i < ARRAY_SIZE(pg_level); i++)
		for (j = 0; j < ARRAY_SIZE(pte_bits); j++)
			pg_level[i].mask |= pte_bits[j].mask;

	debugfs_create_file("kernel_page_tables", 0400, NULL, NULL,
			    &ptdump_fops);

	return 0;
}

device_initcall(ptdump_init);
