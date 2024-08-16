// SPDX-License-Identifier: GPL-2.0
#include <linux/set_memory.h>
#include <linux/ptdump.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/sort.h>
#include <linux/mm.h>
#include <linux/kfence.h>
#include <linux/kasan.h>
#include <asm/kasan.h>
#include <asm/abs_lowcore.h>
#include <asm/nospec-branch.h>
#include <asm/sections.h>
#include <asm/maccess.h>

static unsigned long max_addr;

struct addr_marker {
	int is_start;
	unsigned long start_address;
	const char *name;
};

enum address_markers_idx {
	KVA_NR = 0,
	LOWCORE_START_NR,
	LOWCORE_END_NR,
	AMODE31_START_NR,
	AMODE31_END_NR,
	KERNEL_START_NR,
	KERNEL_END_NR,
#ifdef CONFIG_KFENCE
	KFENCE_START_NR,
	KFENCE_END_NR,
#endif
	IDENTITY_START_NR,
	IDENTITY_END_NR,
	VMEMMAP_NR,
	VMEMMAP_END_NR,
	VMALLOC_NR,
	VMALLOC_END_NR,
#ifdef CONFIG_KMSAN
	KMSAN_VMALLOC_SHADOW_START_NR,
	KMSAN_VMALLOC_SHADOW_END_NR,
	KMSAN_VMALLOC_ORIGIN_START_NR,
	KMSAN_VMALLOC_ORIGIN_END_NR,
	KMSAN_MODULES_SHADOW_START_NR,
	KMSAN_MODULES_SHADOW_END_NR,
	KMSAN_MODULES_ORIGIN_START_NR,
	KMSAN_MODULES_ORIGIN_END_NR,
#endif
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
	[KVA_NR]		= {0, 0, "Kernel Virtual Address Space"},
	[LOWCORE_START_NR]	= {1, 0, "Lowcore Start"},
	[LOWCORE_END_NR]	= {0, 0, "Lowcore End"},
	[IDENTITY_START_NR]	= {1, 0, "Identity Mapping Start"},
	[IDENTITY_END_NR]	= {0, 0, "Identity Mapping End"},
	[AMODE31_START_NR]	= {1, 0, "Amode31 Area Start"},
	[AMODE31_END_NR]	= {0, 0, "Amode31 Area End"},
	[KERNEL_START_NR]	= {1, (unsigned long)_stext, "Kernel Image Start"},
	[KERNEL_END_NR]		= {0, (unsigned long)_end, "Kernel Image End"},
#ifdef CONFIG_KFENCE
	[KFENCE_START_NR]	= {1, 0, "KFence Pool Start"},
	[KFENCE_END_NR]		= {0, 0, "KFence Pool End"},
#endif
	[VMEMMAP_NR]		= {1, 0, "vmemmap Area Start"},
	[VMEMMAP_END_NR]	= {0, 0, "vmemmap Area End"},
	[VMALLOC_NR]		= {1, 0, "vmalloc Area Start"},
	[VMALLOC_END_NR]	= {0, 0, "vmalloc Area End"},
#ifdef CONFIG_KMSAN
	[KMSAN_VMALLOC_SHADOW_START_NR]	= {1, 0, "Kmsan vmalloc Shadow Start"},
	[KMSAN_VMALLOC_SHADOW_END_NR]	= {0, 0, "Kmsan vmalloc Shadow End"},
	[KMSAN_VMALLOC_ORIGIN_START_NR]	= {1, 0, "Kmsan vmalloc Origins Start"},
	[KMSAN_VMALLOC_ORIGIN_END_NR]	= {0, 0, "Kmsan vmalloc Origins End"},
	[KMSAN_MODULES_SHADOW_START_NR]	= {1, 0, "Kmsan Modules Shadow Start"},
	[KMSAN_MODULES_SHADOW_END_NR]	= {0, 0, "Kmsan Modules Shadow End"},
	[KMSAN_MODULES_ORIGIN_START_NR]	= {1, 0, "Kmsan Modules Origins Start"},
	[KMSAN_MODULES_ORIGIN_END_NR]	= {0, 0, "Kmsan Modules Origins End"},
#endif
	[MODULES_NR]		= {1, 0, "Modules Area Start"},
	[MODULES_END_NR]	= {0, 0, "Modules Area End"},
	[ABS_LOWCORE_NR]	= {1, 0, "Lowcore Area Start"},
	[ABS_LOWCORE_END_NR]	= {0, 0, "Lowcore Area End"},
	[MEMCPY_REAL_NR]	= {1, 0, "Real Memory Copy Area Start"},
	[MEMCPY_REAL_END_NR]	= {0, 0, "Real Memory Copy Area End"},
#ifdef CONFIG_KASAN
	[KASAN_SHADOW_START_NR]	= {1, KASAN_SHADOW_START, "Kasan Shadow Start"},
	[KASAN_SHADOW_END_NR]	= {0, KASAN_SHADOW_END, "Kasan Shadow End"},
#endif
	{1, -1UL, NULL}
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
	WARN_ONCE(IS_ENABLED(CONFIG_DEBUG_WX),
		  "s390/mm: Found insecure W+X mapping at address %pS\n",
		  (void *)st->start_address);
	st->wx_pages += (addr - st->start_address) / PAGE_SIZE;
}

static void note_page_update_state(struct pg_state *st, unsigned long addr, unsigned int prot, int level)
{
	struct seq_file *m = st->seq;

	while (addr >= st->marker[1].start_address) {
		st->marker++;
		pt_dump_seq_printf(m, "---[ %s ]---\n", st->marker->name);
	}
	st->start_address = addr;
	st->current_prot = prot;
	st->level = level;
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
		note_page_update_state(st, addr, prot, level);
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
		note_page_update_state(st, addr, prot, level);
	}
}

bool ptdump_check_wx(void)
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
		return true;
	ptdump_walk_pgd(&st.ptdump, &init_mm, NULL);
	if (st.wx_pages) {
		pr_warn("Checked W+X mappings: FAILED, %lu W+X pages found\n", st.wx_pages);

		return false;
	} else {
		pr_info("Checked W+X mappings: passed, no %sW+X pages found\n",
			(nospec_uses_trampoline() || !static_key_enabled(&cpu_has_bear)) ?
			"unexpected " : "");

		return true;
	}
}

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

static int ptdump_cmp(const void *a, const void *b)
{
	const struct addr_marker *ama = a;
	const struct addr_marker *amb = b;

	if (ama->start_address > amb->start_address)
		return 1;
	if (ama->start_address < amb->start_address)
		return -1;
	/*
	 * If the start addresses of two markers are identical consider the
	 * marker which defines the start of an area higher than the one which
	 * defines the end of an area. This keeps pairs of markers sorted.
	 */
	if (ama->is_start)
		return 1;
	if (amb->is_start)
		return -1;
	return 0;
}

static int pt_dump_init(void)
{
#ifdef CONFIG_KFENCE
	unsigned long kfence_start = (unsigned long)__kfence_pool;
#endif
	unsigned long lowcore = (unsigned long)get_lowcore();

	/*
	 * Figure out the maximum virtual address being accessible with the
	 * kernel ASCE. We need this to keep the page table walker functions
	 * from accessing non-existent entries.
	 */
	max_addr = (get_lowcore()->kernel_asce.val & _REGION_ENTRY_TYPE_MASK) >> 2;
	max_addr = 1UL << (max_addr * 11 + 31);
	address_markers[LOWCORE_START_NR].start_address = lowcore;
	address_markers[LOWCORE_END_NR].start_address = lowcore + sizeof(struct lowcore);
	address_markers[IDENTITY_START_NR].start_address = __identity_base;
	address_markers[IDENTITY_END_NR].start_address = __identity_base + ident_map_size;
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
#ifdef CONFIG_KMSAN
	address_markers[KMSAN_VMALLOC_SHADOW_START_NR].start_address = KMSAN_VMALLOC_SHADOW_START;
	address_markers[KMSAN_VMALLOC_SHADOW_END_NR].start_address = KMSAN_VMALLOC_SHADOW_END;
	address_markers[KMSAN_VMALLOC_ORIGIN_START_NR].start_address = KMSAN_VMALLOC_ORIGIN_START;
	address_markers[KMSAN_VMALLOC_ORIGIN_END_NR].start_address = KMSAN_VMALLOC_ORIGIN_END;
	address_markers[KMSAN_MODULES_SHADOW_START_NR].start_address = KMSAN_MODULES_SHADOW_START;
	address_markers[KMSAN_MODULES_SHADOW_END_NR].start_address = KMSAN_MODULES_SHADOW_END;
	address_markers[KMSAN_MODULES_ORIGIN_START_NR].start_address = KMSAN_MODULES_ORIGIN_START;
	address_markers[KMSAN_MODULES_ORIGIN_END_NR].start_address = KMSAN_MODULES_ORIGIN_END;
#endif
	sort(address_markers, ARRAY_SIZE(address_markers) - 1,
	     sizeof(address_markers[0]), ptdump_cmp, NULL);
#ifdef CONFIG_PTDUMP_DEBUGFS
	debugfs_create_file("kernel_page_tables", 0400, NULL, NULL, &ptdump_fops);
#endif /* CONFIG_PTDUMP_DEBUGFS */
	return 0;
}
device_initcall(pt_dump_init);
