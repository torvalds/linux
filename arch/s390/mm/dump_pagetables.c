// SPDX-License-Identifier: GPL-2.0
#include <linux/set_memory.h>
#include <linux/ptdump.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/kasan.h>
#include <asm/kasan.h>
#include <asm/sections.h>

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
	struct ptdump_state ptdump;
	struct seq_file *seq;
	int level;
	unsigned int current_prot;
	unsigned long start_address;
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

static void note_page(struct ptdump_state *pt_st, unsigned long addr, int level, u64 val)
{
	int width = sizeof(unsigned long) * 2;
	static const char units[] = "KMGTPE";
	const char *unit = units;
	unsigned long delta;
	struct pg_state *st;
	struct seq_file *m;
	unsigned int prot;

	st = container_of(pt_st, struct pg_state, ptdump);
	m = st->seq;
	prot = val & (_PAGE_PROTECT | _PAGE_NOEXEC);
	if (level == 4 && (val & _PAGE_INVALID))
		prot = _PAGE_INVALID;
	/* For pmd_none() & friends val gets passed as zero. */
	if (level != 4 && !val)
		prot = _PAGE_INVALID;
	/* Final flush from generic code. */
	if (level == -1)
		addr = max_addr;
	if (st->level == -1) {
		seq_printf(m, "---[ %s ]---\n", st->marker->name);
		st->start_address = addr;
		st->current_prot = prot;
		st->level = level;
	} else if (prot != st->current_prot || level != st->level ||
		   addr >= st->marker[1].start_address) {
		seq_printf(m, "0x%0*lx-0x%0*lx ",
			   width, st->start_address,
			   width, addr);
		delta = (addr - st->start_address) >> 10;
		while (!(delta & 0x3ff) && unit[1]) {
			delta >>= 10;
			unit++;
		}
		seq_printf(m, "%9lu%c ", delta, *unit);
		print_prot(m, st->current_prot, st->level);
		while (addr >= st->marker[1].start_address) {
			st->marker++;
			seq_printf(m, "---[ %s ]---\n", st->marker->name);
		}
		st->start_address = addr;
		st->current_prot = prot;
		st->level = level;
	}
}

static int ptdump_show(struct seq_file *m, void *v)
{
	struct pg_state st = {
		.ptdump = {
			.note_page = note_page,
			.range = (struct ptdump_range[]) {
				{.start = 0, .end = max_addr},
				{.start = 0, .end = 0},
			}
		},
		.seq = m,
		.level = -1,
		.current_prot = 0,
		.start_address = 0,
		.marker = address_markers,
	};

	get_online_mems();
	mutex_lock(&cpa_mutex);
	ptdump_walk_pgd(&st.ptdump, &init_mm, NULL);
	mutex_unlock(&cpa_mutex);
	put_online_mems();
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ptdump);

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
