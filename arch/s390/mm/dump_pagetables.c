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
	unsigned long size;
	const char *name;
};

static struct addr_marker *markers;
static unsigned int markers_cnt;

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
		pt_dump_seq_printf(m, "---[ %s %s ]---\n", st->marker->name,
				   st->marker->is_start ? "Start" : "End");
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
		pt_dump_seq_puts(m, "---[ Kernel Virtual Address Space ]---\n");
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
		.marker = markers,
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
	 * If the start addresses of two markers are identical sort markers in an
	 * order that considers areas contained within other areas correctly.
	 */
	if (ama->is_start && amb->is_start) {
		if (ama->size > amb->size)
			return -1;
		if (ama->size < amb->size)
			return 1;
		return 0;
	}
	if (!ama->is_start && !amb->is_start) {
		if (ama->size > amb->size)
			return 1;
		if (ama->size < amb->size)
			return -1;
		return 0;
	}
	if (ama->is_start)
		return 1;
	if (amb->is_start)
		return -1;
	return 0;
}

static int add_marker(unsigned long start, unsigned long end, const char *name)
{
	size_t oldsize, newsize;

	oldsize = markers_cnt * sizeof(*markers);
	newsize = oldsize + 2 * sizeof(*markers);
	if (!oldsize)
		markers = kvmalloc(newsize, GFP_KERNEL);
	else
		markers = kvrealloc(markers, newsize, GFP_KERNEL);
	if (!markers)
		goto error;
	markers[markers_cnt].is_start = 1;
	markers[markers_cnt].start_address = start;
	markers[markers_cnt].size = end - start;
	markers[markers_cnt].name = name;
	markers_cnt++;
	markers[markers_cnt].is_start = 0;
	markers[markers_cnt].start_address = end;
	markers[markers_cnt].size = end - start;
	markers[markers_cnt].name = name;
	markers_cnt++;
	return 0;
error:
	markers_cnt = 0;
	return -ENOMEM;
}

static int pt_dump_init(void)
{
#ifdef CONFIG_KFENCE
	unsigned long kfence_start = (unsigned long)__kfence_pool;
#endif
	unsigned long lowcore = (unsigned long)get_lowcore();
	int rc;

	/*
	 * Figure out the maximum virtual address being accessible with the
	 * kernel ASCE. We need this to keep the page table walker functions
	 * from accessing non-existent entries.
	 */
	max_addr = (get_lowcore()->kernel_asce.val & _REGION_ENTRY_TYPE_MASK) >> 2;
	max_addr = 1UL << (max_addr * 11 + 31);
	/* start + end markers - must be added first */
	rc = add_marker(0, -1UL, NULL);
	rc |= add_marker((unsigned long)_stext, (unsigned long)_end, "Kernel Image");
	rc |= add_marker(lowcore, lowcore + sizeof(struct lowcore), "Lowcore");
	rc |= add_marker(__identity_base, __identity_base + ident_map_size, "Identity Mapping");
	rc |= add_marker((unsigned long)__samode31, (unsigned long)__eamode31, "Amode31 Area");
	rc |= add_marker(MODULES_VADDR, MODULES_END, "Modules Area");
	rc |= add_marker(__abs_lowcore, __abs_lowcore + ABS_LOWCORE_MAP_SIZE, "Lowcore Area");
	rc |= add_marker(__memcpy_real_area, __memcpy_real_area + MEMCPY_REAL_SIZE, "Real Memory Copy Area");
	rc |= add_marker((unsigned long)vmemmap, (unsigned long)vmemmap + vmemmap_size, "vmemmap Area");
	rc |= add_marker(VMALLOC_START, VMALLOC_END, "vmalloc Area");
#ifdef CONFIG_KFENCE
	rc |= add_marker(kfence_start, kfence_start + KFENCE_POOL_SIZE, "KFence Pool");
#endif
#ifdef CONFIG_KMSAN
	rc |= add_marker(KMSAN_VMALLOC_SHADOW_START, KMSAN_VMALLOC_SHADOW_END, "Kmsan vmalloc Shadow");
	rc |= add_marker(KMSAN_VMALLOC_ORIGIN_START, KMSAN_VMALLOC_ORIGIN_END, "Kmsan vmalloc Origins");
	rc |= add_marker(KMSAN_MODULES_SHADOW_START, KMSAN_MODULES_SHADOW_END, "Kmsan Modules Shadow");
	rc |= add_marker(KMSAN_MODULES_ORIGIN_START, KMSAN_MODULES_ORIGIN_END, "Kmsan Modules Origins");
#endif
#ifdef CONFIG_KASAN
	rc |= add_marker(KASAN_SHADOW_START, KASAN_SHADOW_END, "Kasan Shadow");
#endif
	if (rc)
		goto error;
	sort(&markers[1], markers_cnt - 1, sizeof(*markers), ptdump_cmp, NULL);
#ifdef CONFIG_PTDUMP_DEBUGFS
	debugfs_create_file("kernel_page_tables", 0400, NULL, NULL, &ptdump_fops);
#endif /* CONFIG_PTDUMP_DEBUGFS */
	return 0;
error:
	kvfree(markers);
	return -ENOMEM;
}
device_initcall(pt_dump_init);
