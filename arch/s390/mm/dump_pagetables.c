// SPDX-License-Identifier: GPL-2.0
#include <linux/set_memory.h>
#include <linux/ptdump.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/mm.h>
#include <linux/kfence.h>
#include <linux/kasan.h>
#include <asm/ptdump.h>
#include <asm/kasan.h>
#include <asm/abs_lowcore.h>
#include <asm/nospec-branch.h>
#include <asm/sections.h>
#include <asm/maccess.h>

static unsigned long max_addr;

struct addr_marker {
	unsigned long start_address;
	const char *name;
};

enum address_markers_idx {
	IDENTITY_BEFORE_NR = 0,
	IDENTITY_BEFORE_END_NR,
	AMODE31_START_NR,
	AMODE31_END_NR,
	KERNEL_START_NR,
	KERNEL_END_NR,
#ifdef CONFIG_KFENCE
	KFENCE_START_NR,
	KFENCE_END_NR,
#endif
	IDENTITY_AFTER_NR,
	IDENTITY_AFTER_END_NR,
	VMEMMAP_NR,
	VMEMMAP_END_NR,
	VMALLOC_NR,
	VMALLOC_END_NR,
	MODULES_NR,
	MODULES_END_NR,
	ABS_LOWCORE_NR,
	ABS_LOWCORE_END_NR,
	MEMCPY_REAL_NR,
	MEMCPY_REAL_END_NR,
#ifdef CONFIG_KASAN
	KASAN_SHADOW_START_NR,
	KASAN_SHADOW_END_NR,
#endif
};

static struct addr_marker address_markers[] = {
	[IDENTITY_BEFORE_NR]	= {0, "Identity Mapping Start"},
	[IDENTITY_BEFORE_END_NR] = {(unsigned long)_stext, "Identity Mapping End"},
	[AMODE31_START_NR]	= {0, "Amode31 Area Start"},
	[AMODE31_END_NR]	= {0, "Amode31 Area End"},
	[KERNEL_START_NR]	= {(unsigned long)_stext, "Kernel Image Start"},
	[KERNEL_END_NR]		= {(unsigned long)_end, "Kernel Image End"},
#ifdef CONFIG_KFENCE
	[KFENCE_START_NR]	= {0, "KFence Pool Start"},
	[KFENCE_END_NR]		= {0, "KFence Pool End"},
#endif
	[IDENTITY_AFTER_NR]	= {(unsigned long)_end, "Identity Mapping Start"},
	[IDENTITY_AFTER_END_NR]	= {0, "Identity Mapping End"},
	[VMEMMAP_NR]		= {0, "vmemmap Area Start"},
	[VMEMMAP_END_NR]	= {0, "vmemmap Area End"},
	[VMALLOC_NR]		= {0, "vmalloc Area Start"},
	[VMALLOC_END_NR]	= {0, "vmalloc Area End"},
	[MODULES_NR]		= {0, "Modules Area Start"},
	[MODULES_END_NR]	= {0, "Modules Area End"},
	[ABS_LOWCORE_NR]	= {0, "Lowcore Area Start"},
	[ABS_LOWCORE_END_NR]	= {0, "Lowcore Area End"},
	[MEMCPY_REAL_NR]	= {0, "Real Memory Copy Area Start"},
	[MEMCPY_REAL_END_NR]	= {0, "Real Memory Copy Area End"},
#ifdef CONFIG_KASAN
	[KASAN_SHADOW_START_NR]	= {KASAN_SHADOW_START, "Kasan Shadow Start"},
	[KASAN_SHADOW_END_NR]	= {KASAN_SHADOW_END, "Kasan Shadow End"},
#endif
	{ -1, NULL }
};

struct pg_state {
	struct ptdump_state ptdump;
	struct seq_file *seq;
	int level;
	unsigned int current_prot;
	bool check_wx;
	unsigned long wx_pages;
	unsigned long start_address;
	const struct addr_marker *marker;
};

#define pt_dump_seq_printf(m, fmt, args...)	\
({						\
	struct seq_file *__m = (m);		\
						\
	if (__m)				\
		seq_printf(__m, fmt, ##args);	\
})

#define pt_dump_seq_puts(m, fmt)		\
({						\
	struct seq_file *__m = (m);		\
						\
	if (__m)				\
		seq_printf(__m, fmt);		\
})

static void print_prot(struct seq_file *m, unsigned int pr, int level)
{
	static const char * const level_name[] =
		{ "ASCE", "PGD", "PUD", "PMD", "PTE" };

	pt_dump_seq_printf(m, "%s ", level_name[level]);
	if (pr & _PAGE_INVALID) {
		pt_dump_seq_printf(m, "I\n");
		return;
	}
	pt_dump_seq_puts(m, (pr & _PAGE_PROTECT) ? "RO " : "RW ");
	pt_dump_seq_puts(m, (pr & _PAGE_NOEXEC) ? "NX\n" : "X\n");
}

static void note_prot_wx(struct pg_state *st, unsigned long addr)
{
#ifdef CONFIG_DEBUG_WX
	if (!st->check_wx)
		return;
	if (st->current_prot & _PAGE_INVALID)
		return;
	if (st->current_prot & _PAGE_PROTECT)
		return;
	if (st->current_prot & _PAGE_NOEXEC)
		return;
	/*
	 * The first lowcore page is W+X if spectre mitigations are using
	 * trampolines or the BEAR enhancements facility is not installed,
	 * in which case we have two lpswe instructions in lowcore that need
	 * to be executable.
	 */
	if (addr == PAGE_SIZE && (nospec_uses_trampoline() || !static_key_enabled(&cpu_has_bear)))
		return;
	WARN_ONCE(1, "s390/mm: Found insecure W+X mapping at address %pS\n",
		  (void *)st->start_address);
	st->wx_pages += (addr - st->start_address) / PAGE_SIZE;
#endif /* CONFIG_DEBUG_WX */
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
		pt_dump_seq_printf(m, "---[ %s ]---\n", st->marker->name);
		st->start_address = addr;
		st->current_prot = prot;
		st->level = level;
	} else if (prot != st->current_prot || level != st->level ||
		   addr >= st->marker[1].start_address) {
		note_prot_wx(st, addr);
		pt_dump_seq_printf(m, "0x%0*lx-0x%0*lx ",
				   width, st->start_address,
				   width, addr);
		delta = (addr - st->start_address) >> 10;
		while (!(delta & 0x3ff) && unit[1]) {
			delta >>= 10;
			unit++;
		}
		pt_dump_seq_printf(m, "%9lu%c ", delta, *unit);
		print_prot(m, st->current_prot, st->level);
		while (addr >= st->marker[1].start_address) {
			st->marker++;
			pt_dump_seq_printf(m, "---[ %s ]---\n", st->marker->name);
		}
		st->start_address = addr;
		st->current_prot = prot;
		st->level = level;
	}
}

#ifdef CONFIG_DEBUG_WX
void ptdump_check_wx(void)
{
	struct pg_state st = {
		.ptdump = {
			.note_page = note_page,
			.range = (struct ptdump_range[]) {
				{.start = 0, .end = max_addr},
				{.start = 0, .end = 0},
			}
		},
		.seq = NULL,
		.level = -1,
		.current_prot = 0,
		.check_wx = true,
		.wx_pages = 0,
		.start_address = 0,
		.marker = (struct addr_marker[]) {
			{ .start_address =  0, .name = NULL},
			{ .start_address = -1, .name = NULL},
		},
	};

	if (!MACHINE_HAS_NX)
		return;
	ptdump_walk_pgd(&st.ptdump, &init_mm, NULL);
	if (st.wx_pages)
		pr_warn("Checked W+X mappings: FAILED, %lu W+X pages found\n", st.wx_pages);
	else
		pr_info("Checked W+X mappings: passed, no %sW+X pages found\n",
			(nospec_uses_trampoline() || !static_key_enabled(&cpu_has_bear)) ?
			"unexpected " : "");
}
#endif /* CONFIG_DEBUG_WX */

#ifdef CONFIG_PTDUMP_DEBUGFS
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
		.check_wx = false,
		.wx_pages = 0,
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
#endif /* CONFIG_PTDUMP_DEBUGFS */

/*
 * Heapsort from lib/sort.c is not a stable sorting algorithm, do a simple
 * insertion sort to preserve the original order of markers with the same
 * start address.
 */
static void sort_address_markers(void)
{
	struct addr_marker tmp;
	int i, j;

	for (i = 1; i < ARRAY_SIZE(address_markers) - 1; i++) {
		tmp = address_markers[i];
		for (j = i - 1; j >= 0 && address_markers[j].start_address > tmp.start_address; j--)
			address_markers[j + 1] = address_markers[j];
		address_markers[j + 1] = tmp;
	}
}

static int pt_dump_init(void)
{
#ifdef CONFIG_KFENCE
	unsigned long kfence_start = (unsigned long)__kfence_pool;
#endif
	/*
	 * Figure out the maximum virtual address being accessible with the
	 * kernel ASCE. We need this to keep the page table walker functions
	 * from accessing non-existent entries.
	 */
	max_addr = (S390_lowcore.kernel_asce.val & _REGION_ENTRY_TYPE_MASK) >> 2;
	max_addr = 1UL << (max_addr * 11 + 31);
	address_markers[IDENTITY_AFTER_END_NR].start_address = ident_map_size;
	address_markers[AMODE31_START_NR].start_address = (unsigned long)__samode31;
	address_markers[AMODE31_END_NR].start_address = (unsigned long)__eamode31;
	address_markers[MODULES_NR].start_address = MODULES_VADDR;
	address_markers[MODULES_END_NR].start_address = MODULES_END;
	address_markers[ABS_LOWCORE_NR].start_address = __abs_lowcore;
	address_markers[ABS_LOWCORE_END_NR].start_address = __abs_lowcore + ABS_LOWCORE_MAP_SIZE;
	address_markers[MEMCPY_REAL_NR].start_address = __memcpy_real_area;
	address_markers[MEMCPY_REAL_END_NR].start_address = __memcpy_real_area + MEMCPY_REAL_SIZE;
	address_markers[VMEMMAP_NR].start_address = (unsigned long) vmemmap;
	address_markers[VMEMMAP_END_NR].start_address = (unsigned long)vmemmap + vmemmap_size;
	address_markers[VMALLOC_NR].start_address = VMALLOC_START;
	address_markers[VMALLOC_END_NR].start_address = VMALLOC_END;
#ifdef CONFIG_KFENCE
	address_markers[KFENCE_START_NR].start_address = kfence_start;
	address_markers[KFENCE_END_NR].start_address = kfence_start + KFENCE_POOL_SIZE;
#endif
	sort_address_markers();
#ifdef CONFIG_PTDUMP_DEBUGFS
	debugfs_create_file("kernel_page_tables", 0400, NULL, NULL, &ptdump_fops);
#endif /* CONFIG_PTDUMP_DEBUGFS */
	return 0;
}
device_initcall(pt_dump_init);
