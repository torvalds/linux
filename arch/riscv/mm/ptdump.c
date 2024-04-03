// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 SiFive
 */

#include <linux/efi.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/ptdump.h>

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

/* Private information for debugfs */
struct ptd_mm_info {
	struct mm_struct		*mm;
	const struct addr_marker	*markers;
	unsigned long base_addr;
	unsigned long end;
};

enum address_markers_idx {
	FIXMAP_START_NR,
	FIXMAP_END_NR,
	PCI_IO_START_NR,
	PCI_IO_END_NR,
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	VMEMMAP_START_NR,
	VMEMMAP_END_NR,
#endif
	VMALLOC_START_NR,
	VMALLOC_END_NR,
	PAGE_OFFSET_NR,
#ifdef CONFIG_KASAN
	KASAN_SHADOW_START_NR,
	KASAN_SHADOW_END_NR,
#endif
#ifdef CONFIG_64BIT
	MODULES_MAPPING_NR,
	KERNEL_MAPPING_NR,
#endif
	END_OF_SPACE_NR
};

static struct addr_marker address_markers[] = {
	{0, "Fixmap start"},
	{0, "Fixmap end"},
	{0, "PCI I/O start"},
	{0, "PCI I/O end"},
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	{0, "vmemmap start"},
	{0, "vmemmap end"},
#endif
	{0, "vmalloc() area"},
	{0, "vmalloc() end"},
	{0, "Linear mapping"},
#ifdef CONFIG_KASAN
	{0, "Kasan shadow start"},
	{0, "Kasan shadow end"},
#endif
#ifdef CONFIG_64BIT
	{0, "Modules/BPF mapping"},
	{0, "Kernel mapping"},
#endif
	{-1, NULL},
};

static struct ptd_mm_info kernel_ptd_info = {
	.mm		= &init_mm,
	.markers	= address_markers,
	.base_addr	= 0,
	.end		= ULONG_MAX,
};

#ifdef CONFIG_EFI
static struct addr_marker efi_addr_markers[] = {
		{ 0,		"UEFI runtime start" },
		{ SZ_1G,	"UEFI runtime end" },
		{ -1,		NULL }
};

static struct ptd_mm_info efi_ptd_info = {
	.mm		= &efi_mm,
	.markers	= efi_addr_markers,
	.base_addr	= 0,
	.end		= SZ_2G,
};
#endif

/* Page Table Entry */
struct prot_bits {
	u64 mask;
	const char *set;
	const char *clear;
};

static const struct prot_bits pte_bits[] = {
	{
#ifdef CONFIG_64BIT
		.mask = _PAGE_NAPOT,
		.set = "N",
		.clear = ".",
	}, {
		.mask = _PAGE_MTMASK_SVPBMT,
		.set = "MT(%s)",
		.clear = "  ..  ",
	}, {
#endif
		.mask = _PAGE_SOFT,
		.set = "RSW(%d)",
		.clear = "  ..  ",
	}, {
		.mask = _PAGE_DIRTY,
		.set = "D",
		.clear = ".",
	}, {
		.mask = _PAGE_ACCESSED,
		.set = "A",
		.clear = ".",
	}, {
		.mask = _PAGE_GLOBAL,
		.set = "G",
		.clear = ".",
	}, {
		.mask = _PAGE_USER,
		.set = "U",
		.clear = ".",
	}, {
		.mask = _PAGE_EXEC,
		.set = "X",
		.clear = ".",
	}, {
		.mask = _PAGE_WRITE,
		.set = "W",
		.clear = ".",
	}, {
		.mask = _PAGE_READ,
		.set = "R",
		.clear = ".",
	}, {
		.mask = _PAGE_PRESENT,
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
		char s[7];
		unsigned long val;

		val = st->current_prot & pte_bits[i].mask;
		if (val) {
			if (pte_bits[i].mask == _PAGE_SOFT)
				sprintf(s, pte_bits[i].set, val >> 8);
#ifdef CONFIG_64BIT
			else if (pte_bits[i].mask == _PAGE_MTMASK_SVPBMT) {
				if (val == _PAGE_NOCACHE_SVPBMT)
					sprintf(s, pte_bits[i].set, "NC");
				else if (val == _PAGE_IO_SVPBMT)
					sprintf(s, pte_bits[i].set, "IO");
				else
					sprintf(s, pte_bits[i].set, "??");
			}
#endif
			else
				sprintf(s, "%s", pte_bits[i].set);
		} else {
			sprintf(s, "%s", pte_bits[i].clear);
		}

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

static void ptdump_walk(struct seq_file *s, struct ptd_mm_info *pinfo)
{
	struct pg_state st = {
		.seq = s,
		.marker = pinfo->markers,
		.level = -1,
		.ptdump = {
			.note_page = note_page,
			.range = (struct ptdump_range[]) {
				{pinfo->base_addr, pinfo->end},
				{0, 0}
			}
		}
	};

	ptdump_walk_pgd(&st.ptdump, pinfo->mm, NULL);
}

bool ptdump_check_wx(void)
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

	if (st.wx_pages) {
		pr_warn("Checked W+X mappings: failed, %lu W+X pages found\n",
			st.wx_pages);

		return false;
	} else {
		pr_info("Checked W+X mappings: passed, no W+X pages found\n");

		return true;
	}
}

static int ptdump_show(struct seq_file *m, void *v)
{
	ptdump_walk(m, m->private);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(ptdump);

static int __init ptdump_init(void)
{
	unsigned int i, j;

	address_markers[FIXMAP_START_NR].start_address = FIXADDR_START;
	address_markers[FIXMAP_END_NR].start_address = FIXADDR_TOP;
	address_markers[PCI_IO_START_NR].start_address = PCI_IO_START;
	address_markers[PCI_IO_END_NR].start_address = PCI_IO_END;
#ifdef CONFIG_SPARSEMEM_VMEMMAP
	address_markers[VMEMMAP_START_NR].start_address = VMEMMAP_START;
	address_markers[VMEMMAP_END_NR].start_address = VMEMMAP_END;
#endif
	address_markers[VMALLOC_START_NR].start_address = VMALLOC_START;
	address_markers[VMALLOC_END_NR].start_address = VMALLOC_END;
	address_markers[PAGE_OFFSET_NR].start_address = PAGE_OFFSET;
#ifdef CONFIG_KASAN
	address_markers[KASAN_SHADOW_START_NR].start_address = KASAN_SHADOW_START;
	address_markers[KASAN_SHADOW_END_NR].start_address = KASAN_SHADOW_END;
#endif
#ifdef CONFIG_64BIT
	address_markers[MODULES_MAPPING_NR].start_address = MODULES_VADDR;
	address_markers[KERNEL_MAPPING_NR].start_address = kernel_map.virt_addr;
#endif

	kernel_ptd_info.base_addr = KERN_VIRT_START;

	pg_level[1].name = pgtable_l5_enabled ? "P4D" : "PGD";
	pg_level[2].name = pgtable_l4_enabled ? "PUD" : "PGD";

	for (i = 0; i < ARRAY_SIZE(pg_level); i++)
		for (j = 0; j < ARRAY_SIZE(pte_bits); j++)
			pg_level[i].mask |= pte_bits[j].mask;

	debugfs_create_file("kernel_page_tables", 0400, NULL, &kernel_ptd_info,
			    &ptdump_fops);
#ifdef CONFIG_EFI
	if (efi_enabled(EFI_RUNTIME_SERVICES))
		debugfs_create_file("efi_page_tables", 0400, NULL, &efi_ptd_info,
				    &ptdump_fops);
#endif

	return 0;
}

device_initcall(ptdump_init);
