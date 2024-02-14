// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debug helper to dump the current kernel pagetables of the system
 * so that we can see what the various memory ranges are set to.
 *
 * Derived from x86 implementation:
 * (C) Copyright 2008 Intel Corporation
 *
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 */
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/seq_file.h>

#include <asm/domain.h>
#include <asm/fixmap.h>
#include <asm/memory.h>
#include <asm/ptdump.h>

static struct addr_marker address_markers[] = {
#ifdef CONFIG_KASAN
	{ KASAN_SHADOW_START,	"Kasan shadow start"},
	{ KASAN_SHADOW_END,	"Kasan shadow end"},
#endif
	{ MODULES_VADDR,	"Modules" },
	{ PAGE_OFFSET,		"Kernel Mapping" },
	{ 0,			"vmalloc() Area" },
	{ FDT_FIXED_BASE,	"FDT Area" },
	{ FIXADDR_START,	"Fixmap Area" },
	{ VECTORS_BASE,	"Vectors" },
	{ VECTORS_BASE + PAGE_SIZE * 2, "Vectors End" },
	{ -1,			NULL },
};

#define pt_dump_seq_printf(m, fmt, args...) \
({                      \
	if (m)					\
		seq_printf(m, fmt, ##args);	\
})

#define pt_dump_seq_puts(m, fmt)    \
({						\
	if (m)					\
		seq_printf(m, fmt);	\
})

struct pg_state {
	struct seq_file *seq;
	const struct addr_marker *marker;
	unsigned long start_address;
	unsigned level;
	u64 current_prot;
	bool check_wx;
	unsigned long wx_pages;
	const char *current_domain;
};

struct prot_bits {
	u64		mask;
	u64		val;
	const char	*set;
	const char	*clear;
	bool		ro_bit;
	bool		nx_bit;
};

static const struct prot_bits pte_bits[] = {
	{
		.mask	= L_PTE_USER,
		.val	= L_PTE_USER,
		.set	= "USR",
		.clear	= "   ",
	}, {
		.mask	= L_PTE_RDONLY,
		.val	= L_PTE_RDONLY,
		.set	= "ro",
		.clear	= "RW",
		.ro_bit	= true,
	}, {
		.mask	= L_PTE_XN,
		.val	= L_PTE_XN,
		.set	= "NX",
		.clear	= "x ",
		.nx_bit	= true,
	}, {
		.mask	= L_PTE_SHARED,
		.val	= L_PTE_SHARED,
		.set	= "SHD",
		.clear	= "   ",
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_UNCACHED,
		.set	= "SO/UNCACHED",
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_BUFFERABLE,
		.set	= "MEM/BUFFERABLE/WC",
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_WRITETHROUGH,
		.set	= "MEM/CACHED/WT",
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_WRITEBACK,
		.set	= "MEM/CACHED/WBRA",
#ifndef CONFIG_ARM_LPAE
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_MINICACHE,
		.set	= "MEM/MINICACHE",
#endif
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_WRITEALLOC,
		.set	= "MEM/CACHED/WBWA",
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_DEV_SHARED,
		.set	= "DEV/SHARED",
#ifndef CONFIG_ARM_LPAE
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_DEV_NONSHARED,
		.set	= "DEV/NONSHARED",
#endif
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_DEV_WC,
		.set	= "DEV/WC",
	}, {
		.mask	= L_PTE_MT_MASK,
		.val	= L_PTE_MT_DEV_CACHED,
		.set	= "DEV/CACHED",
	},
};

static const struct prot_bits section_bits[] = {
#ifdef CONFIG_ARM_LPAE
	{
		.mask	= PMD_SECT_USER,
		.val	= PMD_SECT_USER,
		.set	= "USR",
	}, {
		.mask	= L_PMD_SECT_RDONLY | PMD_SECT_AP2,
		.val	= L_PMD_SECT_RDONLY | PMD_SECT_AP2,
		.set	= "ro",
		.clear	= "RW",
		.ro_bit	= true,
#elif __LINUX_ARM_ARCH__ >= 6
	{
		.mask	= PMD_SECT_APX | PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.val	= PMD_SECT_APX | PMD_SECT_AP_WRITE,
		.set	= "    ro",
		.ro_bit	= true,
	}, {
		.mask	= PMD_SECT_APX | PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.val	= PMD_SECT_AP_WRITE,
		.set	= "    RW",
	}, {
		.mask	= PMD_SECT_APX | PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.val	= PMD_SECT_AP_READ,
		.set	= "USR ro",
	}, {
		.mask	= PMD_SECT_APX | PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.val	= PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.set	= "USR RW",
#else /* ARMv4/ARMv5  */
	/* These are approximate */
	{
		.mask   = PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.val    = 0,
		.set    = "    ro",
		.ro_bit	= true,
	}, {
		.mask   = PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.val    = PMD_SECT_AP_WRITE,
		.set    = "    RW",
	}, {
		.mask   = PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.val    = PMD_SECT_AP_READ,
		.set    = "USR ro",
	}, {
		.mask   = PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.val    = PMD_SECT_AP_READ | PMD_SECT_AP_WRITE,
		.set    = "USR RW",
#endif
	}, {
		.mask	= PMD_SECT_XN,
		.val	= PMD_SECT_XN,
		.set	= "NX",
		.clear	= "x ",
		.nx_bit	= true,
	}, {
		.mask	= PMD_SECT_S,
		.val	= PMD_SECT_S,
		.set	= "SHD",
		.clear	= "   ",
	},
};

struct pg_level {
	const char *name;
	const struct prot_bits *bits;
	size_t num;
	u64 mask;
	const struct prot_bits *ro_bit;
	const struct prot_bits *nx_bit;
};

static struct pg_level pg_level[] = {
	{
	}, { /* pgd */
	}, { /* p4d */
	}, { /* pud */
	}, { /* pmd */
		.name	= (CONFIG_PGTABLE_LEVELS > 2) ? "PMD" : "PGD",
		.bits	= section_bits,
		.num	= ARRAY_SIZE(section_bits),
	}, { /* pte */
		.name	= "PTE",
		.bits	= pte_bits,
		.num	= ARRAY_SIZE(pte_bits),
	},
};

static void dump_prot(struct pg_state *st, const struct prot_bits *bits, size_t num)
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

static void note_prot_wx(struct pg_state *st, unsigned long addr)
{
	if (!st->check_wx)
		return;
	if ((st->current_prot & pg_level[st->level].ro_bit->mask) ==
				pg_level[st->level].ro_bit->val)
		return;
	if ((st->current_prot & pg_level[st->level].nx_bit->mask) ==
				pg_level[st->level].nx_bit->val)
		return;

	WARN_ONCE(1, "arm/mm: Found insecure W+X mapping at address %pS\n",
			(void *)st->start_address);

	st->wx_pages += (addr - st->start_address) / PAGE_SIZE;
}

static void note_page(struct pg_state *st, unsigned long addr,
		      unsigned int level, u64 val, const char *domain)
{
	static const char units[] = "KMGTPE";
	u64 prot = val & pg_level[level].mask;

	if (!st->level) {
		st->level = level;
		st->current_prot = prot;
		st->current_domain = domain;
		pt_dump_seq_printf(st->seq, "---[ %s ]---\n", st->marker->name);
	} else if (prot != st->current_prot || level != st->level ||
		   domain != st->current_domain ||
		   addr >= st->marker[1].start_address) {
		const char *unit = units;
		unsigned long delta;

		if (st->current_prot) {
			note_prot_wx(st, addr);
			pt_dump_seq_printf(st->seq, "0x%08lx-0x%08lx   ",
				   st->start_address, addr);

			delta = (addr - st->start_address) >> 10;
			while (!(delta & 1023) && unit[1]) {
				delta >>= 10;
				unit++;
			}
			pt_dump_seq_printf(st->seq, "%9lu%c %s", delta, *unit,
					   pg_level[st->level].name);
			if (st->current_domain)
				pt_dump_seq_printf(st->seq, " %s",
							st->current_domain);
			if (pg_level[st->level].bits)
				dump_prot(st, pg_level[st->level].bits, pg_level[st->level].num);
			pt_dump_seq_printf(st->seq, "\n");
		}

		if (addr >= st->marker[1].start_address) {
			st->marker++;
			pt_dump_seq_printf(st->seq, "---[ %s ]---\n",
							st->marker->name);
		}
		st->start_address = addr;
		st->current_prot = prot;
		st->current_domain = domain;
		st->level = level;
	}
}

static void walk_pte(struct pg_state *st, pmd_t *pmd, unsigned long start,
		     const char *domain)
{
	pte_t *pte = pte_offset_kernel(pmd, 0);
	unsigned long addr;
	unsigned i;

	for (i = 0; i < PTRS_PER_PTE; i++, pte++) {
		addr = start + i * PAGE_SIZE;
		note_page(st, addr, 5, pte_val(*pte), domain);
	}
}

static const char *get_domain_name(pmd_t *pmd)
{
#ifndef CONFIG_ARM_LPAE
	switch (pmd_val(*pmd) & PMD_DOMAIN_MASK) {
	case PMD_DOMAIN(DOMAIN_KERNEL):
		return "KERNEL ";
	case PMD_DOMAIN(DOMAIN_USER):
		return "USER   ";
	case PMD_DOMAIN(DOMAIN_IO):
		return "IO     ";
	case PMD_DOMAIN(DOMAIN_VECTORS):
		return "VECTORS";
	default:
		return "unknown";
	}
#endif
	return NULL;
}

static void walk_pmd(struct pg_state *st, pud_t *pud, unsigned long start)
{
	pmd_t *pmd = pmd_offset(pud, 0);
	unsigned long addr;
	unsigned i;
	const char *domain;

	for (i = 0; i < PTRS_PER_PMD; i++, pmd++) {
		addr = start + i * PMD_SIZE;
		domain = get_domain_name(pmd);
		if (pmd_none(*pmd) || pmd_large(*pmd) || !pmd_present(*pmd))
			note_page(st, addr, 4, pmd_val(*pmd), domain);
		else
			walk_pte(st, pmd, addr, domain);

		if (SECTION_SIZE < PMD_SIZE && pmd_large(pmd[1])) {
			addr += SECTION_SIZE;
			pmd++;
			domain = get_domain_name(pmd);
			note_page(st, addr, 4, pmd_val(*pmd), domain);
		}
	}
}

static void walk_pud(struct pg_state *st, p4d_t *p4d, unsigned long start)
{
	pud_t *pud = pud_offset(p4d, 0);
	unsigned long addr;
	unsigned i;

	for (i = 0; i < PTRS_PER_PUD; i++, pud++) {
		addr = start + i * PUD_SIZE;
		if (!pud_none(*pud)) {
			walk_pmd(st, pud, addr);
		} else {
			note_page(st, addr, 3, pud_val(*pud), NULL);
		}
	}
}

static void walk_p4d(struct pg_state *st, pgd_t *pgd, unsigned long start)
{
	p4d_t *p4d = p4d_offset(pgd, 0);
	unsigned long addr;
	unsigned i;

	for (i = 0; i < PTRS_PER_P4D; i++, p4d++) {
		addr = start + i * P4D_SIZE;
		if (!p4d_none(*p4d)) {
			walk_pud(st, p4d, addr);
		} else {
			note_page(st, addr, 2, p4d_val(*p4d), NULL);
		}
	}
}

static void walk_pgd(struct pg_state *st, struct mm_struct *mm,
			unsigned long start)
{
	pgd_t *pgd = pgd_offset(mm, 0UL);
	unsigned i;
	unsigned long addr;

	for (i = 0; i < PTRS_PER_PGD; i++, pgd++) {
		addr = start + i * PGDIR_SIZE;
		if (!pgd_none(*pgd)) {
			walk_p4d(st, pgd, addr);
		} else {
			note_page(st, addr, 1, pgd_val(*pgd), NULL);
		}
	}
}

void ptdump_walk_pgd(struct seq_file *m, struct ptdump_info *info)
{
	struct pg_state st = {
		.seq = m,
		.marker = info->markers,
		.check_wx = false,
	};

	walk_pgd(&st, info->mm, info->base_addr);
	note_page(&st, 0, 0, 0, NULL);
}

static void __init ptdump_initialize(void)
{
	unsigned i, j;

	for (i = 0; i < ARRAY_SIZE(pg_level); i++)
		if (pg_level[i].bits)
			for (j = 0; j < pg_level[i].num; j++) {
				pg_level[i].mask |= pg_level[i].bits[j].mask;
				if (pg_level[i].bits[j].ro_bit)
					pg_level[i].ro_bit = &pg_level[i].bits[j];
				if (pg_level[i].bits[j].nx_bit)
					pg_level[i].nx_bit = &pg_level[i].bits[j];
			}
#ifdef CONFIG_KASAN
	address_markers[4].start_address = VMALLOC_START;
#else
	address_markers[2].start_address = VMALLOC_START;
#endif
}

static struct ptdump_info kernel_ptdump_info = {
	.mm = &init_mm,
	.markers = address_markers,
	.base_addr = 0,
};

void ptdump_check_wx(void)
{
	struct pg_state st = {
		.seq = NULL,
		.marker = (struct addr_marker[]) {
			{ 0, NULL},
			{ -1, NULL},
		},
		.check_wx = true,
	};

	walk_pgd(&st, &init_mm, 0);
	note_page(&st, 0, 0, 0, NULL);
	if (st.wx_pages)
		pr_warn("Checked W+X mappings: FAILED, %lu W+X pages found\n",
			st.wx_pages);
	else
		pr_info("Checked W+X mappings: passed, no W+X pages found\n");
}

static int __init ptdump_init(void)
{
	ptdump_initialize();
	ptdump_debugfs_register(&kernel_ptdump_info, "kernel_page_tables");
	return 0;
}
__initcall(ptdump_init);
