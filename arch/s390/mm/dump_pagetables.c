// SPDX-License-Identifier: GPL-2.0
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/kasan.h>
#include <asm/kasan.h>
#include <asm/sections.h>
#include <asm/pgtable.h>

static unsigned long max_addr;

struct addr_marker {
	unsigned long start_address;
	const char *name;
};

enum address_markers_idx {
	IDENTITY_NR = 0,
	KERNEL_START_NR,
	KERNEL_END_NR,
#ifdef CONFIG_KASAN
	KASAN_SHADOW_START_NR,
	KASAN_SHADOW_END_NR,
#endif
	VMEMMAP_NR,
	VMALLOC_NR,
	MODULES_NR,
};

static struct addr_marker address_markers[] = {
	[IDENTITY_NR]		= {0, "Identity Mapping"},
	[KERNEL_START_NR]	= {(unsigned long)_stext, "Kernel Image Start"},
	[KERNEL_END_NR]		= {(unsigned long)_end, "Kernel Image End"},
#ifdef CONFIG_KASAN
	[KASAN_SHADOW_START_NR]	= {KASAN_SHADOW_START, "Kasan Shadow Start"},
	[KASAN_SHADOW_END_NR]	= {KASAN_SHADOW_END, "Kasan Shadow End"},
#endif
	[VMEMMAP_NR]		= {0, "vmemmap Area"},
	[VMALLOC_NR]		= {0, "vmalloc Area"},
	[MODULES_NR]		= {0, "Modules Area"},
	{ -1, NULL }
};

struct pg_state {
	int level;
	unsigned int current_prot;
	unsigned long start_address;
	unsigned long current_address;
	const struct addr_marker *marker;
};

static void print_prot(struct seq_file *m, unsigned int pr, int level)
{
	static const char * const level_name[] =
		{ "ASCE", "PGD", "PUD", "PMD", "PTE" };

	seq_printf(m, "%s ", level_name[level]);
	if (pr & _PAGE_INVALID) {
		seq_printf(m, "I\n");
		return;
	}
	seq_puts(m, (pr & _PAGE_PROTECT) ? "RO " : "RW ");
	seq_puts(m, (pr & _PAGE_NOEXEC) ? "NX\n" : "X\n");
}

static void note_page(struct seq_file *m, struct pg_state *st,
		     unsigned int new_prot, int level)
{
	static const char units[] = "KMGTPE";
	int width = sizeof(unsigned long) * 2;
	const char *unit = units;
	unsigned int prot, cur;
	unsigned long delta;

	/*
	 * If we have a "break" in the series, we need to flush the state
	 * that we have now. "break" is either changing perms, levels or
	 * address space marker.
	 */
	prot = new_prot;
	cur = st->current_prot;

	if (!st->level) {
		/* First entry */
		st->current_prot = new_prot;
		st->level = level;
		st->marker = address_markers;
		seq_printf(m, "---[ %s ]---\n", st->marker->name);
	} else if (prot != cur || level != st->level ||
		   st->current_address >= st->marker[1].start_address) {
		/* Print the actual finished series */
		seq_printf(m, "0x%0*lx-0x%0*lx ",
			   width, st->start_address,
			   width, st->current_address);
		delta = (st->current_address - st->start_address) >> 10;
		while (!(delta & 0x3ff) && unit[1]) {
			delta >>= 10;
			unit++;
		}
		seq_printf(m, "%9lu%c ", delta, *unit);
		print_prot(m, st->current_prot, st->level);
		while (st->current_address >= st->marker[1].start_address) {
			st->marker++;
			seq_printf(m, "---[ %s ]---\n", st->marker->name);
		}
		st->start_address = st->current_address;
		st->current_prot = new_prot;
		st->level = level;
	}
}

#ifdef CONFIG_KASAN
static void note_kasan_early_shadow_page(struct seq_file *m,
						struct pg_state *st)
{
	unsigned int prot;

	prot = pte_val(*kasan_early_shadow_pte) &
		(_PAGE_PROTECT | _PAGE_INVALID | _PAGE_NOEXEC);
	note_page(m, st, prot, 4);
}
#endif

/*
 * The actual page table walker functions. In order to keep the
 * implementation of print_prot() short, we only check and pass
 * _PAGE_INVALID and _PAGE_PROTECT flags to note_page() if a region,
 * segment or page table entry is invalid or read-only.
 * After all it's just a hint that the current level being walked
 * contains an invalid or read-only entry.
 */
static void walk_pte_level(struct seq_file *m, struct pg_state *st,
			   pmd_t *pmd, unsigned long addr)
{
	unsigned int prot;
	pte_t *pte;
	int i;

	for (i = 0; i < PTRS_PER_PTE && addr < max_addr; i++) {
		st->current_address = addr;
		pte = pte_offset_kernel(pmd, addr);
		prot = pte_val(*pte) &
			(_PAGE_PROTECT | _PAGE_INVALID | _PAGE_NOEXEC);
		note_page(m, st, prot, 4);
		addr += PAGE_SIZE;
	}
}

static void walk_pmd_level(struct seq_file *m, struct pg_state *st,
			   pud_t *pud, unsigned long addr)
{
	unsigned int prot;
	pmd_t *pmd;
	int i;

#ifdef CONFIG_KASAN
	if ((pud_val(*pud) & PAGE_MASK) == __pa(kasan_early_shadow_pmd)) {
		note_kasan_early_shadow_page(m, st);
		return;
	}
#endif

	pmd = pmd_offset(pud, addr);
	for (i = 0; i < PTRS_PER_PMD && addr < max_addr; i++, pmd++) {
		st->current_address = addr;
		if (!pmd_none(*pmd)) {
			if (pmd_large(*pmd)) {
				prot = pmd_val(*pmd) &
					(_SEGMENT_ENTRY_PROTECT |
					 _SEGMENT_ENTRY_NOEXEC);
				note_page(m, st, prot, 3);
			} else
				walk_pte_level(m, st, pmd, addr);
		} else
			note_page(m, st, _PAGE_INVALID, 3);
		addr += PMD_SIZE;
	}
}

static void walk_pud_level(struct seq_file *m, struct pg_state *st,
			   p4d_t *p4d, unsigned long addr)
{
	unsigned int prot;
	pud_t *pud;
	int i;

#ifdef CONFIG_KASAN
	if ((p4d_val(*p4d) & PAGE_MASK) == __pa(kasan_early_shadow_pud)) {
		note_kasan_early_shadow_page(m, st);
		return;
	}
#endif

	pud = pud_offset(p4d, addr);
	for (i = 0; i < PTRS_PER_PUD && addr < max_addr; i++, pud++) {
		st->current_address = addr;
		if (!pud_none(*pud))
			if (pud_large(*pud)) {
				prot = pud_val(*pud) &
					(_REGION_ENTRY_PROTECT |
					 _REGION_ENTRY_NOEXEC);
				note_page(m, st, prot, 2);
			} else
				walk_pmd_level(m, st, pud, addr);
		else
			note_page(m, st, _PAGE_INVALID, 2);
		addr += PUD_SIZE;
	}
}

static void walk_p4d_level(struct seq_file *m, struct pg_state *st,
			   pgd_t *pgd, unsigned long addr)
{
	p4d_t *p4d;
	int i;

#ifdef CONFIG_KASAN
	if ((pgd_val(*pgd) & PAGE_MASK) == __pa(kasan_early_shadow_p4d)) {
		note_kasan_early_shadow_page(m, st);
		return;
	}
#endif

	p4d = p4d_offset(pgd, addr);
	for (i = 0; i < PTRS_PER_P4D && addr < max_addr; i++, p4d++) {
		st->current_address = addr;
		if (!p4d_none(*p4d))
			walk_pud_level(m, st, p4d, addr);
		else
			note_page(m, st, _PAGE_INVALID, 2);
		addr += P4D_SIZE;
	}
}

static void walk_pgd_level(struct seq_file *m)
{
	unsigned long addr = 0;
	struct pg_state st;
	pgd_t *pgd;
	int i;

	memset(&st, 0, sizeof(st));
	for (i = 0; i < PTRS_PER_PGD && addr < max_addr; i++) {
		st.current_address = addr;
		pgd = pgd_offset_k(addr);
		if (!pgd_none(*pgd))
			walk_p4d_level(m, &st, pgd, addr);
		else
			note_page(m, &st, _PAGE_INVALID, 1);
		addr += PGDIR_SIZE;
		cond_resched();
	}
	/* Flush out the last page */
	st.current_address = max_addr;
	note_page(m, &st, 0, 0);
}

static int ptdump_show(struct seq_file *m, void *v)
{
	walk_pgd_level(m);
	return 0;
}

static int ptdump_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, ptdump_show, NULL);
}

static const struct file_operations ptdump_fops = {
	.open		= ptdump_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int pt_dump_init(void)
{
	/*
	 * Figure out the maximum virtual address being accessible with the
	 * kernel ASCE. We need this to keep the page table walker functions
	 * from accessing non-existent entries.
	 */
	max_addr = (S390_lowcore.kernel_asce & _REGION_ENTRY_TYPE_MASK) >> 2;
	max_addr = 1UL << (max_addr * 11 + 31);
	address_markers[MODULES_NR].start_address = MODULES_VADDR;
	address_markers[VMEMMAP_NR].start_address = (unsigned long) vmemmap;
	address_markers[VMALLOC_NR].start_address = VMALLOC_START;
	debugfs_create_file("kernel_page_tables", 0400, NULL, NULL, &ptdump_fops);
	return 0;
}
device_initcall(pt_dump_init);
