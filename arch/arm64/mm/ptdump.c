// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserved.
 * Debug helper to dump the current kernel pagetables of the system
 * so that we can see what the various memory ranges are set to.
 *
 * Derived from x86 and arm implementation:
 * (C) Copyright 2008 Intel Corporation
 *
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 */
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/ptdump.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include <asm/fixmap.h>
#include <asm/kasan.h>
#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>
#include <asm/ptdump.h>


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

static const struct ptdump_prot_bits pte_bits[] = {
	{
		.mask	= PTE_VALID,
		.val	= PTE_VALID,
		.set	= " ",
		.clear	= "F",
	}, {
		.mask	= PTE_USER,
		.val	= PTE_USER,
		.set	= "USR",
		.clear	= "   ",
	}, {
		.mask	= PTE_RDONLY,
		.val	= PTE_RDONLY,
		.set	= "ro",
		.clear	= "RW",
	}, {
		.mask	= PTE_PXN,
		.val	= PTE_PXN,
		.set	= "NX",
		.clear	= "x ",
	}, {
		.mask	= PTE_SHARED,
		.val	= PTE_SHARED,
		.set	= "SHD",
		.clear	= "   ",
	}, {
		.mask	= PTE_AF,
		.val	= PTE_AF,
		.set	= "AF",
		.clear	= "  ",
	}, {
		.mask	= PTE_NG,
		.val	= PTE_NG,
		.set	= "NG",
		.clear	= "  ",
	}, {
		.mask	= PTE_CONT,
		.val	= PTE_CONT,
		.set	= "CON",
		.clear	= "   ",
	}, {
		.mask	= PTE_TABLE_BIT,
		.val	= PTE_TABLE_BIT,
		.set	= "   ",
		.clear	= "BLK",
	}, {
		.mask	= PTE_UXN,
		.val	= PTE_UXN,
		.set	= "UXN",
		.clear	= "   ",
	}, {
		.mask	= PTE_GP,
		.val	= PTE_GP,
		.set	= "GP",
		.clear	= "  ",
	}, {
		.mask	= PTE_ATTRINDX_MASK,
		.val	= PTE_ATTRINDX(MT_DEVICE_nGnRnE),
		.set	= "DEVICE/nGnRnE",
	}, {
		.mask	= PTE_ATTRINDX_MASK,
		.val	= PTE_ATTRINDX(MT_DEVICE_nGnRE),
		.set	= "DEVICE/nGnRE",
	}, {
		.mask	= PTE_ATTRINDX_MASK,
		.val	= PTE_ATTRINDX(MT_NORMAL_NC),
		.set	= "MEM/NORMAL-NC",
	}, {
		.mask	= PTE_ATTRINDX_MASK,
		.val	= PTE_ATTRINDX(MT_NORMAL),
		.set	= "MEM/NORMAL",
	}, {
		.mask	= PTE_ATTRINDX_MASK,
		.val	= PTE_ATTRINDX(MT_NORMAL_TAGGED),
		.set	= "MEM/NORMAL-TAGGED",
	}
};

static struct ptdump_pg_level kernel_pg_levels[] __ro_after_init = {
	{ /* pgd */
		.name	= "PGD",
		.bits	= pte_bits,
		.num	= ARRAY_SIZE(pte_bits),
	}, { /* p4d */
		.name	= "P4D",
		.bits	= pte_bits,
		.num	= ARRAY_SIZE(pte_bits),
	}, { /* pud */
		.name	= "PUD",
		.bits	= pte_bits,
		.num	= ARRAY_SIZE(pte_bits),
	}, { /* pmd */
		.name	= "PMD",
		.bits	= pte_bits,
		.num	= ARRAY_SIZE(pte_bits),
	}, { /* pte */
		.name	= "PTE",
		.bits	= pte_bits,
		.num	= ARRAY_SIZE(pte_bits),
	},
};

static void dump_prot(struct ptdump_pg_state *st, const struct ptdump_prot_bits *bits,
			size_t num)
{
	unsigned i;

	for (i = 0; i < num; i++, bits++) {
		const char *s;

		if ((st->current_prot & bits->mask) == bits->val)
			s = bits->set;
		else
			s = bits->clear;

		if (s)
			pt_dump_seq_printf(st->seq, " %s", s);
	}
}

static void note_prot_uxn(struct ptdump_pg_state *st, unsigned long addr)
{
	if (!st->check_wx)
		return;

	if ((st->current_prot & PTE_UXN) == PTE_UXN)
		return;

	WARN_ONCE(1, "arm64/mm: Found non-UXN mapping at address %p/%pS\n",
		  (void *)st->start_address, (void *)st->start_address);

	st->uxn_pages += (addr - st->start_address) / PAGE_SIZE;
}

static void note_prot_wx(struct ptdump_pg_state *st, unsigned long addr)
{
	if (!st->check_wx)
		return;
	if ((st->current_prot & PTE_RDONLY) == PTE_RDONLY)
		return;
	if ((st->current_prot & PTE_PXN) == PTE_PXN)
		return;

	WARN_ONCE(1, "arm64/mm: Found insecure W+X mapping at address %p/%pS\n",
		  (void *)st->start_address, (void *)st->start_address);

	st->wx_pages += (addr - st->start_address) / PAGE_SIZE;
}

void note_page(struct ptdump_state *pt_st, unsigned long addr, int level,
	       u64 val)
{
	struct ptdump_pg_state *st = container_of(pt_st, struct ptdump_pg_state, ptdump);
	struct ptdump_pg_level *pg_level = st->pg_level;
	static const char units[] = "KMGTPE";
	u64 prot = 0;

	/* check if the current level has been folded dynamically */
	if (st->mm && ((level == 1 && mm_p4d_folded(st->mm)) ||
	    (level == 2 && mm_pud_folded(st->mm))))
		level = 0;

	if (level >= 0)
		prot = val & pg_level[level].mask;

	if (st->level == -1) {
		st->level = level;
		st->current_prot = prot;
		st->start_address = addr;
		pt_dump_seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
	} else if (prot != st->current_prot || level != st->level ||
		   addr >= st->marker[1].start_address) {
		const char *unit = units;
		unsigned long delta;

		if (st->current_prot) {
			note_prot_uxn(st, addr);
			note_prot_wx(st, addr);
		}

		pt_dump_seq_printf(st->seq, "0x%016lx-0x%016lx   ",
				   st->start_address, addr);

		delta = (addr - st->start_address) >> 10;
		while (!(delta & 1023) && unit[1]) {
			delta >>= 10;
			unit++;
		}
		pt_dump_seq_printf(st->seq, "%9lu%c %s", delta, *unit,
				   pg_level[st->level].name);
		if (st->current_prot && pg_level[st->level].bits)
			dump_prot(st, pg_level[st->level].bits,
				  pg_level[st->level].num);
		pt_dump_seq_puts(st->seq, "\n");

		if (addr >= st->marker[1].start_address) {
			st->marker++;
			pt_dump_seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
		}

		st->start_address = addr;
		st->current_prot = prot;
		st->level = level;
	}

	if (addr >= st->marker[1].start_address) {
		st->marker++;
		pt_dump_seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
	}

}

void ptdump_walk(struct seq_file *s, struct ptdump_info *info)
{
	unsigned long end = ~0UL;
	struct ptdump_pg_state st;

	if (info->base_addr < TASK_SIZE_64)
		end = TASK_SIZE_64;

	st = (struct ptdump_pg_state){
		.seq = s,
		.marker = info->markers,
		.mm = info->mm,
		.pg_level = &kernel_pg_levels[0],
		.level = -1,
		.ptdump = {
			.note_page = note_page,
			.range = (struct ptdump_range[]){
				{info->base_addr, end},
				{0, 0}
			}
		}
	};

	ptdump_walk_pgd(&st.ptdump, info->mm, NULL);
}

static void __init ptdump_initialize(void)
{
	unsigned i, j;

	for (i = 0; i < ARRAY_SIZE(kernel_pg_levels); i++)
		if (kernel_pg_levels[i].bits)
			for (j = 0; j < kernel_pg_levels[i].num; j++)
				kernel_pg_levels[i].mask |= kernel_pg_levels[i].bits[j].mask;
}

static struct ptdump_info kernel_ptdump_info __ro_after_init = {
	.mm		= &init_mm,
};

bool ptdump_check_wx(void)
{
	struct ptdump_pg_state st = {
		.seq = NULL,
		.marker = (struct addr_marker[]) {
			{ 0, NULL},
			{ -1, NULL},
		},
		.pg_level = &kernel_pg_levels[0],
		.level = -1,
		.check_wx = true,
		.ptdump = {
			.note_page = note_page,
			.range = (struct ptdump_range[]) {
				{_PAGE_OFFSET(vabits_actual), ~0UL},
				{0, 0}
			}
		}
	};

	ptdump_walk_pgd(&st.ptdump, &init_mm, NULL);

	if (st.wx_pages || st.uxn_pages) {
		pr_warn("Checked W+X mappings: FAILED, %lu W+X pages found, %lu non-UXN pages found\n",
			st.wx_pages, st.uxn_pages);

		return false;
	} else {
		pr_info("Checked W+X mappings: passed, no W+X pages found\n");

		return true;
	}
}

static int __init ptdump_init(void)
{
	u64 page_offset = _PAGE_OFFSET(vabits_actual);
	u64 vmemmap_start = (u64)virt_to_page((void *)page_offset);
	struct addr_marker m[] = {
		{ PAGE_OFFSET,		"Linear Mapping start" },
		{ PAGE_END,		"Linear Mapping end" },
#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
		{ KASAN_SHADOW_START,   "Kasan shadow start" },
		{ KASAN_SHADOW_END,     "Kasan shadow end" },
#endif
		{ MODULES_VADDR,	"Modules start" },
		{ MODULES_END,		"Modules end" },
		{ VMALLOC_START,	"vmalloc() area" },
		{ VMALLOC_END,		"vmalloc() end" },
		{ vmemmap_start,	"vmemmap start" },
		{ VMEMMAP_END,		"vmemmap end" },
		{ PCI_IO_START,		"PCI I/O start" },
		{ PCI_IO_END,		"PCI I/O end" },
		{ FIXADDR_TOT_START,    "Fixmap start" },
		{ FIXADDR_TOP,	        "Fixmap end" },
		{ -1,			NULL },
	};
	static struct addr_marker address_markers[ARRAY_SIZE(m)] __ro_after_init;

	kernel_ptdump_info.markers = memcpy(address_markers, m, sizeof(m));
	kernel_ptdump_info.base_addr = page_offset;

	ptdump_initialize();
	ptdump_debugfs_register(&kernel_ptdump_info, "kernel_page_tables");
	return 0;
}
device_initcall(ptdump_init);
