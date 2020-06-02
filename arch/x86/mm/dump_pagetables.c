// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debug helper to dump the current kernel pagetables of the system
 * so that we can see what the various memory ranges are set to.
 *
 * (C) Copyright 2008 Intel Corporation
 *
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 */

#include <linux/debugfs.h>
#include <linux/kasan.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/ptdump.h>

#include <asm/e820/types.h>
#include <asm/pgtable.h>

/*
 * The dumper groups pagetable entries of the same type into one, and for
 * that it needs to keep some state when walking, and flush this state
 * when a "break" in the continuity is found.
 */
struct pg_state {
	struct ptdump_state ptdump;
	int level;
	pgprotval_t current_prot;
	pgprotval_t effective_prot;
	pgprotval_t prot_levels[5];
	unsigned long start_address;
	const struct addr_marker *marker;
	unsigned long lines;
	bool to_dmesg;
	bool check_wx;
	unsigned long wx_pages;
	struct seq_file *seq;
};

struct addr_marker {
	unsigned long start_address;
	const char *name;
	unsigned long max_lines;
};

/* Address space markers hints */

#ifdef CONFIG_X86_64

enum address_markers_idx {
	USER_SPACE_NR = 0,
	KERNEL_SPACE_NR,
#ifdef CONFIG_MODIFY_LDT_SYSCALL
	LDT_NR,
#endif
	LOW_KERNEL_NR,
	VMALLOC_START_NR,
	VMEMMAP_START_NR,
#ifdef CONFIG_KASAN
	KASAN_SHADOW_START_NR,
	KASAN_SHADOW_END_NR,
#endif
	CPU_ENTRY_AREA_NR,
#ifdef CONFIG_X86_ESPFIX64
	ESPFIX_START_NR,
#endif
#ifdef CONFIG_EFI
	EFI_END_NR,
#endif
	HIGH_KERNEL_NR,
	MODULES_VADDR_NR,
	MODULES_END_NR,
	FIXADDR_START_NR,
	END_OF_SPACE_NR,
};

static struct addr_marker address_markers[] = {
	[USER_SPACE_NR]		= { 0,			"User Space" },
	[KERNEL_SPACE_NR]	= { (1UL << 63),	"Kernel Space" },
	[LOW_KERNEL_NR]		= { 0UL,		"Low Kernel Mapping" },
	[VMALLOC_START_NR]	= { 0UL,		"vmalloc() Area" },
	[VMEMMAP_START_NR]	= { 0UL,		"Vmemmap" },
#ifdef CONFIG_KASAN
	/*
	 * These fields get initialized with the (dynamic)
	 * KASAN_SHADOW_{START,END} values in pt_dump_init().
	 */
	[KASAN_SHADOW_START_NR]	= { 0UL,		"KASAN shadow" },
	[KASAN_SHADOW_END_NR]	= { 0UL,		"KASAN shadow end" },
#endif
#ifdef CONFIG_MODIFY_LDT_SYSCALL
	[LDT_NR]		= { 0UL,		"LDT remap" },
#endif
	[CPU_ENTRY_AREA_NR]	= { CPU_ENTRY_AREA_BASE,"CPU entry Area" },
#ifdef CONFIG_X86_ESPFIX64
	[ESPFIX_START_NR]	= { ESPFIX_BASE_ADDR,	"ESPfix Area", 16 },
#endif
#ifdef CONFIG_EFI
	[EFI_END_NR]		= { EFI_VA_END,		"EFI Runtime Services" },
#endif
	[HIGH_KERNEL_NR]	= { __START_KERNEL_map,	"High Kernel Mapping" },
	[MODULES_VADDR_NR]	= { MODULES_VADDR,	"Modules" },
	[MODULES_END_NR]	= { MODULES_END,	"End Modules" },
	[FIXADDR_START_NR]	= { FIXADDR_START,	"Fixmap Area" },
	[END_OF_SPACE_NR]	= { -1,			NULL }
};

#define INIT_PGD	((pgd_t *) &init_top_pgt)

#else /* CONFIG_X86_64 */

enum address_markers_idx {
	USER_SPACE_NR = 0,
	KERNEL_SPACE_NR,
	VMALLOC_START_NR,
	VMALLOC_END_NR,
#ifdef CONFIG_HIGHMEM
	PKMAP_BASE_NR,
#endif
#ifdef CONFIG_MODIFY_LDT_SYSCALL
	LDT_NR,
#endif
	CPU_ENTRY_AREA_NR,
	FIXADDR_START_NR,
	END_OF_SPACE_NR,
};

static struct addr_marker address_markers[] = {
	[USER_SPACE_NR]		= { 0,			"User Space" },
	[KERNEL_SPACE_NR]	= { PAGE_OFFSET,	"Kernel Mapping" },
	[VMALLOC_START_NR]	= { 0UL,		"vmalloc() Area" },
	[VMALLOC_END_NR]	= { 0UL,		"vmalloc() End" },
#ifdef CONFIG_HIGHMEM
	[PKMAP_BASE_NR]		= { 0UL,		"Persistent kmap() Area" },
#endif
#ifdef CONFIG_MODIFY_LDT_SYSCALL
	[LDT_NR]		= { 0UL,		"LDT remap" },
#endif
	[CPU_ENTRY_AREA_NR]	= { 0UL,		"CPU entry area" },
	[FIXADDR_START_NR]	= { 0UL,		"Fixmap area" },
	[END_OF_SPACE_NR]	= { -1,			NULL }
};

#define INIT_PGD	(swapper_pg_dir)

#endif /* !CONFIG_X86_64 */

/* Multipliers for offsets within the PTEs */
#define PTE_LEVEL_MULT (PAGE_SIZE)
#define PMD_LEVEL_MULT (PTRS_PER_PTE * PTE_LEVEL_MULT)
#define PUD_LEVEL_MULT (PTRS_PER_PMD * PMD_LEVEL_MULT)
#define P4D_LEVEL_MULT (PTRS_PER_PUD * PUD_LEVEL_MULT)
#define PGD_LEVEL_MULT (PTRS_PER_P4D * P4D_LEVEL_MULT)

#define pt_dump_seq_printf(m, to_dmesg, fmt, args...)		\
({								\
	if (to_dmesg)					\
		printk(KERN_INFO fmt, ##args);			\
	else							\
		if (m)						\
			seq_printf(m, fmt, ##args);		\
})

#define pt_dump_cont_printf(m, to_dmesg, fmt, args...)		\
({								\
	if (to_dmesg)					\
		printk(KERN_CONT fmt, ##args);			\
	else							\
		if (m)						\
			seq_printf(m, fmt, ##args);		\
})

/*
 * Print a readable form of a pgprot_t to the seq_file
 */
static void printk_prot(struct seq_file *m, pgprotval_t pr, int level, bool dmsg)
{
	static const char * const level_name[] =
		{ "pgd", "p4d", "pud", "pmd", "pte" };

	if (!(pr & _PAGE_PRESENT)) {
		/* Not present */
		pt_dump_cont_printf(m, dmsg, "                              ");
	} else {
		if (pr & _PAGE_USER)
			pt_dump_cont_printf(m, dmsg, "USR ");
		else
			pt_dump_cont_printf(m, dmsg, "    ");
		if (pr & _PAGE_RW)
			pt_dump_cont_printf(m, dmsg, "RW ");
		else
			pt_dump_cont_printf(m, dmsg, "ro ");
		if (pr & _PAGE_PWT)
			pt_dump_cont_printf(m, dmsg, "PWT ");
		else
			pt_dump_cont_printf(m, dmsg, "    ");
		if (pr & _PAGE_PCD)
			pt_dump_cont_printf(m, dmsg, "PCD ");
		else
			pt_dump_cont_printf(m, dmsg, "    ");

		/* Bit 7 has a different meaning on level 3 vs 4 */
		if (level <= 3 && pr & _PAGE_PSE)
			pt_dump_cont_printf(m, dmsg, "PSE ");
		else
			pt_dump_cont_printf(m, dmsg, "    ");
		if ((level == 4 && pr & _PAGE_PAT) ||
		    ((level == 3 || level == 2) && pr & _PAGE_PAT_LARGE))
			pt_dump_cont_printf(m, dmsg, "PAT ");
		else
			pt_dump_cont_printf(m, dmsg, "    ");
		if (pr & _PAGE_GLOBAL)
			pt_dump_cont_printf(m, dmsg, "GLB ");
		else
			pt_dump_cont_printf(m, dmsg, "    ");
		if (pr & _PAGE_NX)
			pt_dump_cont_printf(m, dmsg, "NX ");
		else
			pt_dump_cont_printf(m, dmsg, "x  ");
	}
	pt_dump_cont_printf(m, dmsg, "%s\n", level_name[level]);
}

static void note_wx(struct pg_state *st, unsigned long addr)
{
	unsigned long npages;

	npages = (addr - st->start_address) / PAGE_SIZE;

#ifdef CONFIG_PCI_BIOS
	/*
	 * If PCI BIOS is enabled, the PCI BIOS area is forced to WX.
	 * Inform about it, but avoid the warning.
	 */
	if (pcibios_enabled && st->start_address >= PAGE_OFFSET + BIOS_BEGIN &&
	    addr <= PAGE_OFFSET + BIOS_END) {
		pr_warn_once("x86/mm: PCI BIOS W+X mapping %lu pages\n", npages);
		return;
	}
#endif
	/* Account the WX pages */
	st->wx_pages += npages;
	WARN_ONCE(__supported_pte_mask & _PAGE_NX,
		  "x86/mm: Found insecure W+X mapping at address %pS\n",
		  (void *)st->start_address);
}

static void effective_prot(struct ptdump_state *pt_st, int level, u64 val)
{
	struct pg_state *st = container_of(pt_st, struct pg_state, ptdump);
	pgprotval_t prot = val & PTE_FLAGS_MASK;
	pgprotval_t effective;

	if (level > 0) {
		pgprotval_t higher_prot = st->prot_levels[level - 1];

		effective = (higher_prot & prot & (_PAGE_USER | _PAGE_RW)) |
			    ((higher_prot | prot) & _PAGE_NX);
	} else {
		effective = prot;
	}

	st->prot_levels[level] = effective;
}

/*
 * This function gets called on a break in a continuous series
 * of PTE entries; the next one is different so we need to
 * print what we collected so far.
 */
static void note_page(struct ptdump_state *pt_st, unsigned long addr, int level,
		      unsigned long val)
{
	struct pg_state *st = container_of(pt_st, struct pg_state, ptdump);
	pgprotval_t new_prot, new_eff;
	pgprotval_t cur, eff;
	static const char units[] = "BKMGTPE";
	struct seq_file *m = st->seq;

	new_prot = val & PTE_FLAGS_MASK;
	if (!val)
		new_eff = 0;
	else
		new_eff = st->prot_levels[level];

	/*
	 * If we have a "break" in the series, we need to flush the state that
	 * we have now. "break" is either changing perms, levels or
	 * address space marker.
	 */
	cur = st->current_prot;
	eff = st->effective_prot;

	if (st->level == -1) {
		/* First entry */
		st->current_prot = new_prot;
		st->effective_prot = new_eff;
		st->level = level;
		st->marker = address_markers;
		st->lines = 0;
		pt_dump_seq_printf(m, st->to_dmesg, "---[ %s ]---\n",
				   st->marker->name);
	} else if (new_prot != cur || new_eff != eff || level != st->level ||
		   addr >= st->marker[1].start_address) {
		const char *unit = units;
		unsigned long delta;
		int width = sizeof(unsigned long) * 2;

		if (st->check_wx && (eff & _PAGE_RW) && !(eff & _PAGE_NX))
			note_wx(st, addr);

		/*
		 * Now print the actual finished series
		 */
		if (!st->marker->max_lines ||
		    st->lines < st->marker->max_lines) {
			pt_dump_seq_printf(m, st->to_dmesg,
					   "0x%0*lx-0x%0*lx   ",
					   width, st->start_address,
					   width, addr);

			delta = addr - st->start_address;
			while (!(delta & 1023) && unit[1]) {
				delta >>= 10;
				unit++;
			}
			pt_dump_cont_printf(m, st->to_dmesg, "%9lu%c ",
					    delta, *unit);
			printk_prot(m, st->current_prot, st->level,
				    st->to_dmesg);
		}
		st->lines++;

		/*
		 * We print markers for special areas of address space,
		 * such as the start of vmalloc space etc.
		 * This helps in the interpretation.
		 */
		if (addr >= st->marker[1].start_address) {
			if (st->marker->max_lines &&
			    st->lines > st->marker->max_lines) {
				unsigned long nskip =
					st->lines - st->marker->max_lines;
				pt_dump_seq_printf(m, st->to_dmesg,
						   "... %lu entr%s skipped ... \n",
						   nskip,
						   nskip == 1 ? "y" : "ies");
			}
			st->marker++;
			st->lines = 0;
			pt_dump_seq_printf(m, st->to_dmesg, "---[ %s ]---\n",
					   st->marker->name);
		}

		st->start_address = addr;
		st->current_prot = new_prot;
		st->effective_prot = new_eff;
		st->level = level;
	}
}

static void ptdump_walk_pgd_level_core(struct seq_file *m,
				       struct mm_struct *mm, pgd_t *pgd,
				       bool checkwx, bool dmesg)
{
	const struct ptdump_range ptdump_ranges[] = {
#ifdef CONFIG_X86_64
	{0, PTRS_PER_PGD * PGD_LEVEL_MULT / 2},
	{GUARD_HOLE_END_ADDR, ~0UL},
#else
	{0, ~0UL},
#endif
	{0, 0}
};

	struct pg_state st = {
		.ptdump = {
			.note_page	= note_page,
			.effective_prot = effective_prot,
			.range		= ptdump_ranges
		},
		.level = -1,
		.to_dmesg	= dmesg,
		.check_wx	= checkwx,
		.seq		= m
	};

	ptdump_walk_pgd(&st.ptdump, mm, pgd);

	if (!checkwx)
		return;
	if (st.wx_pages)
		pr_info("x86/mm: Checked W+X mappings: FAILED, %lu W+X pages found.\n",
			st.wx_pages);
	else
		pr_info("x86/mm: Checked W+X mappings: passed, no W+X pages found.\n");
}

void ptdump_walk_pgd_level(struct seq_file *m, struct mm_struct *mm)
{
	ptdump_walk_pgd_level_core(m, mm, mm->pgd, false, true);
}

void ptdump_walk_pgd_level_debugfs(struct seq_file *m, struct mm_struct *mm,
				   bool user)
{
	pgd_t *pgd = mm->pgd;
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	if (user && boot_cpu_has(X86_FEATURE_PTI))
		pgd = kernel_to_user_pgdp(pgd);
#endif
	ptdump_walk_pgd_level_core(m, mm, pgd, false, false);
}
EXPORT_SYMBOL_GPL(ptdump_walk_pgd_level_debugfs);

void ptdump_walk_user_pgd_level_checkwx(void)
{
#ifdef CONFIG_PAGE_TABLE_ISOLATION
	pgd_t *pgd = INIT_PGD;

	if (!(__supported_pte_mask & _PAGE_NX) ||
	    !boot_cpu_has(X86_FEATURE_PTI))
		return;

	pr_info("x86/mm: Checking user space page tables\n");
	pgd = kernel_to_user_pgdp(pgd);
	ptdump_walk_pgd_level_core(NULL, &init_mm, pgd, true, false);
#endif
}

void ptdump_walk_pgd_level_checkwx(void)
{
	ptdump_walk_pgd_level_core(NULL, &init_mm, INIT_PGD, true, false);
}

static int __init pt_dump_init(void)
{
	/*
	 * Various markers are not compile-time constants, so assign them
	 * here.
	 */
#ifdef CONFIG_X86_64
	address_markers[LOW_KERNEL_NR].start_address = PAGE_OFFSET;
	address_markers[VMALLOC_START_NR].start_address = VMALLOC_START;
	address_markers[VMEMMAP_START_NR].start_address = VMEMMAP_START;
#ifdef CONFIG_MODIFY_LDT_SYSCALL
	address_markers[LDT_NR].start_address = LDT_BASE_ADDR;
#endif
#ifdef CONFIG_KASAN
	address_markers[KASAN_SHADOW_START_NR].start_address = KASAN_SHADOW_START;
	address_markers[KASAN_SHADOW_END_NR].start_address = KASAN_SHADOW_END;
#endif
#endif
#ifdef CONFIG_X86_32
	address_markers[VMALLOC_START_NR].start_address = VMALLOC_START;
	address_markers[VMALLOC_END_NR].start_address = VMALLOC_END;
# ifdef CONFIG_HIGHMEM
	address_markers[PKMAP_BASE_NR].start_address = PKMAP_BASE;
# endif
	address_markers[FIXADDR_START_NR].start_address = FIXADDR_START;
	address_markers[CPU_ENTRY_AREA_NR].start_address = CPU_ENTRY_AREA_BASE;
# ifdef CONFIG_MODIFY_LDT_SYSCALL
	address_markers[LDT_NR].start_address = LDT_BASE_ADDR;
# endif
#endif
	return 0;
}
__initcall(pt_dump_init);
