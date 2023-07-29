// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/align.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/mman.h>
#include <linux/seq_buf.h>
#include <linux/vmalloc.h>
#include <linux/cma.h>
#include <linux/slab.h>
#include <linux/page_ext.h>
#include <linux/page_owner.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <soc/qcom/minidump.h>
#include <linux/dma-map-ops.h>
#include <linux/jhash.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/fdtable.h>
#include <linux/qcom_dma_heap.h>
#include "debug_symbol.h"
#include "minidump_memory.h"
#include "../../../mm/slab.h"
#include "../mm/internal.h"

static unsigned long *md_debug_totalcma_pages;
static struct list_head *md_debug_slab_caches;
static struct mutex *md_debug_slab_mutex;
static struct static_key *md_debug_page_owner_inited;
static struct static_key *md_debug_slub_debug_enabled;
static unsigned long *md_debug_min_low_pfn;
static unsigned long *md_debug_max_pfn;

#define DMA_BUF_HASH_SIZE (1 << 20)
#define DMA_BUF_HASH_SEED 0x9747b28c
static bool dma_buf_hash[DMA_BUF_HASH_SIZE];

struct priv_buf {
	char *buf;
	size_t size;
	size_t offset;
};

struct dma_buf_priv {
	struct priv_buf *priv_buf;
	struct task_struct *task;
	int count;
	size_t size;
};

static void show_val_kb(struct seq_buf *m, const char *s, unsigned long num)
{
	seq_buf_printf(m, "%s : %lld KB\n", s, num << (PAGE_SHIFT - 10));
}

void md_dump_meminfo(struct seq_buf *m)
{
	struct sysinfo i;
	long cached;
	long available;
	unsigned long pages[NR_LRU_LISTS];
	unsigned long sreclaimable, sunreclaim;
	int lru;

	si_meminfo(&i);
	si_swapinfo(&i);

	cached = global_node_page_state(NR_FILE_PAGES) -
			total_swapcache_pages() - i.bufferram;
	if (cached < 0)
		cached = 0;

	for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
		pages[lru] = global_node_page_state(NR_LRU_BASE + lru);

	available = si_mem_available();
	sreclaimable = global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B);
	sunreclaim = global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B);

	show_val_kb(m, "MemTotal:       ", i.totalram);
	show_val_kb(m, "MemFree:        ", i.freeram);
	show_val_kb(m, "MemAvailable:   ", available);
	show_val_kb(m, "Buffers:        ", i.bufferram);
	show_val_kb(m, "Cached:         ", cached);
	show_val_kb(m, "SwapCached:     ", total_swapcache_pages());
	show_val_kb(m, "Active:         ", pages[LRU_ACTIVE_ANON] +
					   pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive:       ", pages[LRU_INACTIVE_ANON] +
					   pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Active(anon):   ", pages[LRU_ACTIVE_ANON]);
	show_val_kb(m, "Inactive(anon): ", pages[LRU_INACTIVE_ANON]);
	show_val_kb(m, "Active(file):   ", pages[LRU_ACTIVE_FILE]);
	show_val_kb(m, "Inactive(file): ", pages[LRU_INACTIVE_FILE]);
	show_val_kb(m, "Unevictable:    ", pages[LRU_UNEVICTABLE]);
	show_val_kb(m, "Mlocked:        ", global_zone_page_state(NR_MLOCK));

#ifdef CONFIG_HIGHMEM
	show_val_kb(m, "HighTotal:      ", i.totalhigh);
	show_val_kb(m, "HighFree:       ", i.freehigh);
	show_val_kb(m, "LowTotal:       ", i.totalram - i.totalhigh);
	show_val_kb(m, "LowFree:        ", i.freeram - i.freehigh);
#endif

	show_val_kb(m, "SwapTotal:      ", i.totalswap);
	show_val_kb(m, "SwapFree:       ", i.freeswap);
	show_val_kb(m, "Dirty:          ",
		    global_node_page_state(NR_FILE_DIRTY));
	show_val_kb(m, "Writeback:      ",
		    global_node_page_state(NR_WRITEBACK));
	show_val_kb(m, "AnonPages:      ",
		    global_node_page_state(NR_ANON_MAPPED));
	show_val_kb(m, "Mapped:         ",
		    global_node_page_state(NR_FILE_MAPPED));
	show_val_kb(m, "Shmem:          ", i.sharedram);
	show_val_kb(m, "KReclaimable:   ", sreclaimable +
		    global_node_page_state(NR_KERNEL_MISC_RECLAIMABLE));
	show_val_kb(m, "Slab:           ", sreclaimable + sunreclaim);
	show_val_kb(m, "SReclaimable:   ", sreclaimable);
	show_val_kb(m, "SUnreclaim:     ", sunreclaim);
	seq_buf_printf(m, "KernelStack:    %8lu kB\n",
		   global_node_page_state(NR_KERNEL_STACK_KB));
#ifdef CONFIG_SHADOW_CALL_STACK
	seq_buf_printf(m, "ShadowCallStack:%8lu kB\n",
		   global_node_page_state(NR_KERNEL_SCS_KB));
#endif
	show_val_kb(m, "PageTables:     ",
		    global_node_page_state(NR_PAGETABLE));
	show_val_kb(m, "Bounce:         ",
		    global_zone_page_state(NR_BOUNCE));
	show_val_kb(m, "WritebackTmp:   ",
		    global_node_page_state(NR_WRITEBACK_TEMP));
	seq_buf_printf(m, "VmallocTotal:   %8lu kB\n",
		   (unsigned long)VMALLOC_TOTAL >> 10);
	show_val_kb(m, "VmallocUsed: ", vmalloc_nr_pages());
	show_val_kb(m, "Percpu:         ", pcpu_nr_pages());

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	show_val_kb(m, "AnonHugePages:  ",
		    global_node_page_state(NR_ANON_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "ShmemHugePages: ",
		    global_node_page_state(NR_SHMEM_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "ShmemPmdMapped: ",
		    global_node_page_state(NR_SHMEM_PMDMAPPED) * HPAGE_PMD_NR);
	show_val_kb(m, "FileHugePages:  ",
		    global_node_page_state(NR_FILE_THPS) * HPAGE_PMD_NR);
	show_val_kb(m, "FilePmdMapped:  ",
		    global_node_page_state(NR_FILE_PMDMAPPED) * HPAGE_PMD_NR);
#endif

#ifdef CONFIG_CMA
	show_val_kb(m, "CmaTotal:       ", *md_debug_totalcma_pages);
	show_val_kb(m, "CmaFree:        ",
		    global_zone_page_state(NR_FREE_CMA_PAGES));
#endif
}

#ifdef CONFIG_SLUB_DEBUG
static void slabinfo_stats(struct seq_buf *m, struct kmem_cache *cachep)
{
#ifdef CONFIG_DEBUG_SLAB
	{			/* node stats */
		unsigned long high = cachep->high_mark;
		unsigned long allocs = cachep->num_allocations;
		unsigned long grown = cachep->grown;
		unsigned long reaped = cachep->reaped;
		unsigned long errors = cachep->errors;
		unsigned long max_freeable = cachep->max_freeable;
		unsigned long node_allocs = cachep->node_allocs;
		unsigned long node_frees = cachep->node_frees;
		unsigned long overflows = cachep->node_overflow;

		seq_buf_printf(m,
				" : globalstat %7lu %6lu %5lu %4lu %4lu %4lu %4lu %4lu %4lu",
				allocs, high, grown,
				reaped, errors, max_freeable,
				node_allocs, node_frees, overflows);
	}
	/* cpu stats */
	{
		unsigned long allochit = atomic_read(&cachep->allochit);
		unsigned long allocmiss = atomic_read(&cachep->allocmiss);
		unsigned long freehit = atomic_read(&cachep->freehit);
		unsigned long freemiss = atomic_read(&cachep->freemiss);

		seq_buf_printf(m,
				" : cpustat %6lu %6lu %6lu %6lu",
				allochit, allocmiss, freehit, freemiss);
	}
#endif
}

void md_dump_slabinfo(struct seq_buf *m)
{
	struct kmem_cache *s;
	struct slabinfo sinfo;

	if (!md_debug_slab_caches)
		return;

	if (!md_debug_slab_mutex)
		return;

	if (!mutex_trylock(md_debug_slab_mutex))
		return;

	/* print_slabinfo_header */
	seq_buf_printf(m,
			"# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab>");
	seq_buf_printf(m,
			" : tunables <limit> <batchcount> <sharedfactor>");
	seq_buf_printf(m,
			" : slabdata <active_slabs> <num_slabs> <sharedavail>");
#ifdef CONFIG_DEBUG_SLAB
	seq_buf_printf(m,
			" : globalstat <listallocs> <maxobjs> <grown> <reaped> <error> <maxfreeable> <nodeallocs> <remotefrees> <alienoverflow>");
	seq_buf_printf(m,
			" : cpustat <allochit> <allocmiss> <freehit> <freemiss>");
#endif
	seq_buf_printf(m, "\n");

	/* Loop through all slabs */
	list_for_each_entry(s, md_debug_slab_caches, list) {
		memset(&sinfo, 0, sizeof(sinfo));
		get_slabinfo(s, &sinfo);

		seq_buf_printf(m, "%-17s %6lu %6lu %6u %4u %4d",
		   s->name, sinfo.active_objs, sinfo.num_objs, s->size,
		   sinfo.objects_per_slab, (1 << sinfo.cache_order));

		seq_buf_printf(m, " : tunables %4u %4u %4u",
		   sinfo.limit, sinfo.batchcount, sinfo.shared);
		seq_buf_printf(m, " : slabdata %6lu %6lu %6lu",
		   sinfo.active_slabs, sinfo.num_slabs, sinfo.shared_avail);
		slabinfo_stats(m, s);
		seq_buf_printf(m, "\n");
	}
	mutex_unlock(md_debug_slab_mutex);
}
#endif

bool md_register_memory_dump(int size, char *name)
{
	struct md_region md_entry;
	void *buffer_start;
	struct page *page;
	int ret;

	page  = cma_alloc(dma_contiguous_default_area, size >> PAGE_SHIFT,
			0, GFP_KERNEL);

	if (!page) {
		pr_err("Failed to allocate %s minidump, increase cma size\n",
			name);
		return false;
	}

	buffer_start = page_to_virt(page);
	strscpy(md_entry.name, name, sizeof(md_entry.name));
	md_entry.virt_addr = (uintptr_t) buffer_start;
	md_entry.phys_addr = virt_to_phys(buffer_start);
	md_entry.size = size;
	ret = msm_minidump_add_region(&md_entry);
	if (ret < 0) {
		cma_release(dma_contiguous_default_area, page, size >> PAGE_SHIFT);
		pr_err("Failed to add %s entry in Minidump\n", name);
		return false;
	}
	memset(buffer_start, 0, size);

	/* Complete registration before adding enteries */
	smp_mb();

#ifdef CONFIG_PAGE_OWNER
	if (!strcmp(name, "PAGEOWNER"))
		WRITE_ONCE(md_pageowner_dump_addr, buffer_start);
#endif
#ifdef CONFIG_SLUB_DEBUG
	if (!strcmp(name, "SLABOWNER"))
		WRITE_ONCE(md_slabowner_dump_addr, buffer_start);
#endif
	if (!strcmp(name, "DMA_INFO"))
		WRITE_ONCE(md_dma_buf_info_addr, buffer_start);
	if (!strcmp(name, "DMA_PROC"))
		WRITE_ONCE(md_dma_buf_procs_addr, buffer_start);
	return true;
}

bool md_unregister_memory_dump(char *name)
{
	struct page *page;
	struct md_region mdr;
	struct md_region md_entry;

	mdr = md_get_region(name);
	if (!mdr.virt_addr) {
		pr_err("minidump entry for %s not found\n", name);
		return false;
	}
	strscpy(md_entry.name, mdr.name, sizeof(md_entry.name));
	md_entry.virt_addr = mdr.virt_addr;
	md_entry.phys_addr = mdr.phys_addr;
	md_entry.size = mdr.size;
	page = virt_to_page(mdr.virt_addr);

	if (msm_minidump_remove_region(&md_entry) < 0)
		return false;

	cma_release(dma_contiguous_default_area, page,
			(md_entry.size) >> PAGE_SHIFT);
	return true;
}

static void update_dump_size(char *name, size_t size, char **addr, size_t *dump_size)
{
	if ((*dump_size) == 0) {
		if (md_register_memory_dump(size * SZ_1M,
						name)) {
			*dump_size = size * SZ_1M;
			pr_info_ratelimited("%s Minidump set to %zd MB size\n",
					name, size);
		}
		return;
	}
	if (md_unregister_memory_dump(name)) {
		*addr = NULL;
		if (size == 0) {
			*dump_size = 0;
			pr_info_ratelimited("%s Minidump : disabled\n", name);
			return;
		}
		if (md_register_memory_dump(size * SZ_1M,
						name)) {
			*dump_size = size * SZ_1M;
			pr_info_ratelimited("%s Minidump : set to %zd MB\n",
					name, size);
		} else if (md_register_memory_dump(*dump_size,
							name)) {
			pr_info_ratelimited("%s Minidump : Fallback to %zd MB\n",
					name, (*dump_size) / SZ_1M);
		} else {
			pr_err_ratelimited("%s Minidump : disabled, Can't fallback to %zd MB,\n",
						name, (*dump_size) / SZ_1M);
			*dump_size = 0;
		}
	} else {
		pr_err_ratelimited("Failed to unregister %s Minidump\n", name);
	}
}

#ifdef CONFIG_PAGE_OWNER
static unsigned long page_owner_filter = 0xF;
static unsigned long page_owner_handles_size =  SZ_16K;
static int nr_page_owner_handles, nr_slab_owner_handles;
static LIST_HEAD(accounted_call_site_list);
static DEFINE_MUTEX(accounted_call_site_lock);
struct accounted_call_site {
	struct list_head list;
	char name[50];
};

bool is_page_owner_enabled(void)
{
	if (md_debug_page_owner_inited &&
		atomic_read(&md_debug_page_owner_inited->enabled))
		return true;
	return false;

}

static bool found_stack(depot_stack_handle_t handle,
		 char *dump_addr, size_t dump_size,
		 unsigned long handles_size, int *nr_handles)
{
	int *handles, i;

	handles = (int *) (dump_addr +
			dump_size - handles_size);

	for (i = 0; i < *nr_handles; i++)
		if (handle == handles[i])
			return true;

	if ((handles + *nr_handles) < (int *)(dump_addr + dump_size)) {
		handles[*nr_handles] = handle;
		*nr_handles += 1;
	} else {
		pr_err_ratelimited("Can't stores handles increase handles size\n");
	}
	return false;
}

static bool check_unaccounted(char *buf, ssize_t count,
		struct page *page, depot_stack_handle_t handle)
{
	int i, ret = 0;
	unsigned long *entries;
	unsigned int nr_entries;
	struct accounted_call_site *call_site;

	if ((page->flags &
		((1UL << PG_lru) | (1UL << PG_slab) | (1UL << PG_swapbacked))))
		return false;

	nr_entries = stack_depot_fetch(handle, &entries);
	for (i = 0; i < nr_entries; i++) {
		ret = scnprintf(buf, count, "%pS\n",
				(void *)entries[i]);
		if (ret == count - 1)
			return false;

		mutex_lock(&accounted_call_site_lock);
		list_for_each_entry(call_site,
				&accounted_call_site_list, list) {
			if (strnstr(buf, call_site->name,
					strlen(buf))) {
				mutex_unlock(&accounted_call_site_lock);
				return false;
			}
		}
		mutex_unlock(&accounted_call_site_lock);
	}
	return true;
}

static ssize_t dump_page_owner_md(char *buf, size_t count,
		unsigned long pfn, struct page *page,
		depot_stack_handle_t handle)
{
	int i, bit, ret = 0;
	unsigned long *entries;
	unsigned int nr_entries;

	if (page_owner_filter == 0xF)
		goto dump;

	for (bit = 1; page_owner_filter >= bit; bit *= 2) {
		if (page_owner_filter & bit) {
			switch (bit) {
			case 0x1:
				if (check_unaccounted(buf, count, page, handle))
					goto dump;
				break;
			case 0x2:
				if (page->flags & (1UL << PG_slab))
					goto dump;
				break;
			case 0x4:
				if (page->flags & (1UL << PG_swapbacked))
					goto dump;
				break;
			case 0x8:
				if ((page->flags & (1UL << PG_lru)) &&
					~(page->flags & (1UL << PG_swapbacked)))
					goto dump;
				break;
			default:
				break;
			}
		}
		if (bit >= 0x8)
			return ret;
	}

	if (bit > page_owner_filter)
		return ret;
dump:
	nr_entries = stack_depot_fetch(handle, &entries);
	if ((buf > (md_pageowner_dump_addr +
			md_pageowner_dump_size - page_owner_handles_size))
			|| !found_stack(handle,
				md_pageowner_dump_addr,
				md_pageowner_dump_size,
				page_owner_handles_size,
				&nr_page_owner_handles)) {
		ret = scnprintf(buf, count, "%lu %u %u\n",
				pfn, handle, nr_entries);
		if (ret == count - 1)
			goto err;

		for (i = 0; i < nr_entries; i++) {
			ret += scnprintf(buf + ret, count - ret,
					"%p\n", (void *)entries[i]);
			if (ret == count - 1)
				goto err;
		}
	} else {
		ret = scnprintf(buf, count, "%lu %u %u\n",  pfn, handle, 0);
	}
err:
	return ret;
}

void md_dump_pageowner(char *addr, size_t dump_size)
{
	unsigned long pfn;
	struct page *page;
	struct page_ext *page_ext;
	depot_stack_handle_t handle;
	ssize_t size;

	if (!md_debug_min_low_pfn)
		return;

	if (!md_debug_max_pfn)
		return;

	page = NULL;
	pfn = *md_debug_min_low_pfn;

	/* Find a valid PFN or the start of a MAX_ORDER_NR_PAGES area */
	while (!pfn_valid(pfn) && (pfn & (MAX_ORDER_NR_PAGES - 1)) != 0)
		pfn++;

	/* Find an allocated page */
	for (; pfn < *md_debug_max_pfn; pfn++) {
		/*
		 * If the new page is in a new MAX_ORDER_NR_PAGES area,
		 * validate the area as existing, skip it if not
		 */
		if ((pfn & (MAX_ORDER_NR_PAGES - 1)) == 0 && !pfn_valid(pfn)) {
			pfn += MAX_ORDER_NR_PAGES - 1;
			continue;
		}

		page = pfn_to_page(pfn);
		if (PageBuddy(page)) {
			unsigned long freepage_order = buddy_order_unsafe(page);

			if (freepage_order < MAX_ORDER)
				pfn += (1UL << freepage_order) - 1;
			continue;
		}

		page_ext = page_ext_get(page);
		if (unlikely(!page_ext))
			goto next;

		/*
		 * Some pages could be missed by concurrent allocation or free,
		 * because we don't hold the zone lock.
		 */
		if (!test_bit(PAGE_EXT_OWNER, &page_ext->flags))
			goto next;

		/*
		 * Although we do have the info about past allocation of free
		 * pages, it's not relevant for current memory usage.
		 */
		if (!test_bit(PAGE_EXT_OWNER_ALLOCATED, &page_ext->flags))
			goto next;

		handle = get_page_owner_handle(page_ext, pfn);
		if (!handle)
			goto next;

		size = dump_page_owner_md(addr, dump_size, pfn, page, handle);
		if (size == dump_size - 1) {
			pr_err("pageowner minidump region exhausted\n");
			page_ext_put(page_ext);
			return;
		}

		dump_size -= size;
		addr += size;
next:
		page_ext_put(page_ext);
	}
}

static DEFINE_MUTEX(page_owner_dump_size_lock);

static ssize_t page_owner_dump_size_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long long  size;

	if (kstrtoull_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for size\n");
		return -EINVAL;
	}
	mutex_lock(&page_owner_dump_size_lock);
	update_dump_size("PAGEOWNER", size,
			&md_pageowner_dump_addr, &md_pageowner_dump_size);
	mutex_unlock(&page_owner_dump_size_lock);
	return count;
}

static ssize_t page_owner_dump_size_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "%llu MB\n",
			md_pageowner_dump_size / SZ_1M);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_page_owner_dump_size_ops = {
	.open	= simple_open,
	.write	= page_owner_dump_size_write,
	.read	= page_owner_dump_size_read,
};

static ssize_t page_owner_filter_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long filter;

	if (kstrtoul_from_user(ubuf, count, 0, &filter)) {
		pr_err_ratelimited("Invalid format for filter\n");
		return -EINVAL;
	}

	if (filter & (~0xF)) {
		pr_err_ratelimited("Invalid filter : use following filters or any combinations of these\n"
				"0x1 - unaccounted\n"
				"0x2 - slab\n"
				"0x4 - Anon\n"
				"0x8 - File\n");
		return -EINVAL;
	}
	page_owner_filter = filter;
	return count;
}

static ssize_t page_owner_filter_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[64];

	snprintf(buf, sizeof(buf), "0x%lx\n", page_owner_filter);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_page_owner_filter_ops = {
	.open	= simple_open,
	.write	= page_owner_filter_write,
	.read	= page_owner_filter_read,
};

static ssize_t page_owner_handle_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long size;

	if (kstrtoul_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for handle size\n");
		return -EINVAL;
	}

	if (size) {
		if (size > (md_pageowner_dump_size / SZ_16K)) {
			pr_err_ratelimited("size : %lu KB exceeds max size : %lu KB\n",
				size, (md_pageowner_dump_size / SZ_16K));
			goto err;
		}
		page_owner_handles_size = size * SZ_1K;
	}
err:
	return count;
}

static ssize_t page_owner_handle_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[64];

	snprintf(buf, sizeof(buf), "%lu KB\n",
			(page_owner_handles_size / SZ_1K));
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_page_owner_handle_ops = {
	.open	= simple_open,
	.write	= page_owner_handle_write,
	.read	= page_owner_handle_read,
};

static ssize_t page_owner_call_site_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	struct accounted_call_site *call_site;
	char buf[50];

	if (count >= 50) {
		pr_err_ratelimited("Input string size too large\n");
		return -EINVAL;
	}

	memset(buf, 0, 50);

	if (copy_from_user(buf, ubuf, count)) {
		pr_err_ratelimited("Couldn't copy from user\n");
		return -EFAULT;
	}

	if (!isalpha(buf[0]) && buf[0] != '_') {
		pr_err_ratelimited("Invalid call site name\n");
		return -EINVAL;
	}

	call_site = kzalloc(sizeof(*call_site), GFP_KERNEL);
	if (!call_site)
		return -ENOMEM;

	strscpy(call_site->name, buf, strlen(call_site->name));
	mutex_lock(&accounted_call_site_lock);
	list_add_tail(&call_site->list, &accounted_call_site_list);
	mutex_unlock(&accounted_call_site_lock);

	return count;
}

static ssize_t page_owner_call_site_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char *kbuf;
	struct accounted_call_site *call_site;
	int i = 1, ret = 0;
	size_t size = PAGE_SIZE;

	kbuf = kmalloc(size, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	ret = scnprintf(kbuf, count, "%s\n", "Accounted call sites:");
	mutex_lock(&accounted_call_site_lock);
	list_for_each_entry(call_site, &accounted_call_site_list, list) {
		ret += scnprintf(kbuf + ret, size - ret,
			"%d. %s\n", i, call_site->name);
		i += 1;
		if (ret == size) {
			ret = -ENOMEM;
			mutex_unlock(&accounted_call_site_lock);
			goto err;
		}
	}
	mutex_unlock(&accounted_call_site_lock);
	ret = simple_read_from_buffer(ubuf, count, offset, kbuf, strlen(kbuf));
err:
	kfree(kbuf);
	return ret;
}

static const struct file_operations proc_page_owner_call_site_ops = {
	.open	= simple_open,
	.write	= page_owner_call_site_write,
	.read	= page_owner_call_site_read,
};

void md_debugfs_pageowner(struct dentry *minidump_dir)
{
	debugfs_create_file("page_owner_dump_size_mb", 0400, minidump_dir, NULL,
			&proc_page_owner_dump_size_ops);
	debugfs_create_file("page_owner_filter", 0400, minidump_dir, NULL,
		    &proc_page_owner_filter_ops);
	debugfs_create_file("page_owner_handles_size_kb", 0400, minidump_dir, NULL,
			&proc_page_owner_handle_ops);
	debugfs_create_file("page_owner_call_sites", 0400, minidump_dir, NULL,
			&proc_page_owner_call_site_ops);
}
#endif

#ifdef CONFIG_SLUB_DEBUG
#define STACK_HASH_SEED 0x9747b28c

static unsigned long slab_owner_filter;
static unsigned long slab_owner_handles_size = SZ_16K;

bool is_slub_debug_enabled(void)
{
	if (md_debug_slub_debug_enabled &&
		atomic_read(&md_debug_slub_debug_enabled->enabled))
		return true;
	return false;
}

static int dump_tracking(const struct kmem_cache *s,
		const void *object,
		const struct track *t, void *private)
{
	int ret = 0;
	u32 nr_entries;
	struct priv_buf *priv_buf;
	char *buf;
	size_t size;
	unsigned long *entries;

	if (!t->addr || !t->handle)
		return 0;

	priv_buf = (struct priv_buf *)private;
	buf = priv_buf->buf + priv_buf->offset;
	size = priv_buf->size - priv_buf->offset;
#ifdef CONFIG_STACKDEPOT
	{
		int i;
		nr_entries = stack_depot_fetch(t->handle, &entries);

		if ((buf > (md_slabowner_dump_addr +
			md_slabowner_dump_size - slab_owner_handles_size))
			|| !found_stack(t->handle,
				md_slabowner_dump_addr,
				md_slabowner_dump_size,
				slab_owner_handles_size,
				&nr_slab_owner_handles)) {

			ret = scnprintf(buf, size, "%p %p %u\n",
				object, t->handle, nr_entries);
			if (ret == size - 1)
				goto err;

			for (i = 0; i < nr_entries; i++) {
				ret += scnprintf(buf + ret, size - ret,
						"%p\n", (void *)entries[i]);
				if (ret == size - 1)
					goto err;
			}
		} else {
			ret = scnprintf(buf, size, "%p %p %u\n",
					object, t->handle, 0);
		}
	}
#else
	ret = scnprintf(buf, size, "%p %p\n", object, (void *)t->addr);

#endif
err:
	priv_buf->offset += ret;
	return ret;
}

void md_dump_slabowner(char *m, size_t dump_size)
{
	struct kmem_cache *s;
	int node;
	struct priv_buf buf;
	struct kmem_cache_node *n;
	ssize_t ret;
	int i;

	buf.buf = m;
	buf.size = dump_size;
	buf.offset = 0;

	for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
		if (!test_bit(i, &slab_owner_filter))
			continue;
		s = kmalloc_caches[KMALLOC_NORMAL][i];
		if (!s)
			continue;
		ret = scnprintf(buf.buf, buf.size, "%s\n", s->name);
		if (ret == buf.size - 1)
			return;
		buf.buf += ret;
		for_each_kmem_cache_node(s, node, n) {
			unsigned long flags;
			struct page *page;

			if (!atomic_long_read(&n->nr_slabs))
				continue;

			spin_lock_irqsave(&n->list_lock, flags);
			list_for_each_entry(page, &n->partial, lru) {
				ret  = get_each_object_track(s, page, TRACK_ALLOC,
						dump_tracking, &buf);
				if (buf.offset == buf.size - 1) {
					spin_unlock_irqrestore(&n->list_lock, flags);
					pr_err("slabowner minidump region exhausted\n");
					return;
				}
			}
			list_for_each_entry(page, &n->full, lru) {
				ret  = get_each_object_track(s, page, TRACK_ALLOC,
						dump_tracking, &buf);
				if (buf.offset == buf.size - 1) {
					spin_unlock_irqrestore(&n->list_lock, flags);
					pr_err("slabowner minidump region exhausted\n");
					return;
				}
			}
			spin_unlock_irqrestore(&n->list_lock, flags);
		}
		ret = scnprintf(buf.buf, buf.size, "\n");
		if (ret == buf.size - 1)
			return;
		buf.buf += ret;
	}
}

static ssize_t slab_owner_dump_size_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long long  size;

	if (kstrtoull_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for size\n");
		return -EINVAL;
	}
	update_dump_size("SLABOWNER", size,
			&md_slabowner_dump_addr, &md_slabowner_dump_size);
	return count;
}

static ssize_t slab_owner_dump_size_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "%llu MB\n", md_slabowner_dump_size/SZ_1M);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_slab_owner_dump_size_ops = {
	.open	= simple_open,
	.write	= slab_owner_dump_size_write,
	.read	= slab_owner_dump_size_read,
};

static ssize_t slab_owner_filter_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long filter;
	int bit, i;
	struct kmem_cache *s;

	if (kstrtoul_from_user(ubuf, count, 0, &filter)) {
		pr_err_ratelimited("Invalid format for filter\n");
		return -EINVAL;
	}

	for (i = 0, bit = 1; filter >= bit; bit *= 2, i++) {
		if (filter & bit) {
			s = kmalloc_caches[KMALLOC_NORMAL][i];
			if (!s) {
				pr_err("Invalid filter : %lx kmalloc-%d doesn't exist\n",
						filter, bit);
				return -EINVAL;
			}
		}
	}
	slab_owner_filter = filter;
	return count;
}

static ssize_t slab_owner_filter_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[64];

	snprintf(buf, sizeof(buf), "0x%lx\n", slab_owner_filter);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_slab_owner_filter_ops = {
	.open	= simple_open,
	.write	= slab_owner_filter_write,
	.read	= slab_owner_filter_read,
};

static ssize_t slab_owner_handle_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long size;

	if (kstrtoul_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for handle size\n");
		return -EINVAL;
	}

	if (size) {
		if (size > (md_slabowner_dump_size / SZ_16K)) {
			pr_err_ratelimited("size : %lu KB exceeds max size : %lu KB\n",
				size, (md_slabowner_dump_size / SZ_16K));
			goto err;
		}
		slab_owner_handles_size = size * SZ_1K;
	}
err:
	return count;
}

static ssize_t slab_owner_handle_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[64];

	snprintf(buf, sizeof(buf), "%lu KB\n",
			(slab_owner_handles_size / SZ_1K));
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_slab_owner_handle_ops = {
	.open	= simple_open,
	.write	= slab_owner_handle_write,
	.read	= slab_owner_handle_read,
};

void md_debugfs_slabowner(struct dentry *minidump_dir)
{
	int i;

	debugfs_create_file("slab_owner_dump_size_mb", 0400, minidump_dir, NULL,
		    &proc_slab_owner_dump_size_ops);
	debugfs_create_file("slab_owner_filter", 0400, minidump_dir, NULL,
		    &proc_slab_owner_filter_ops);
	debugfs_create_file("slab_owner_handles_size_kb", 0400,
			minidump_dir, NULL, &proc_slab_owner_handle_ops);
	for (i = 0; i <= KMALLOC_SHIFT_HIGH; i++) {
		if (kmalloc_caches[KMALLOC_NORMAL][i])
			set_bit(i, &slab_owner_filter);
	}
}
#endif	/* CONFIG_SLUB_DEBUG */

static int dump_bufinfo(const struct dma_buf *buf_obj, void *private)
{
	int ret;
	struct dma_buf_attachment *attach_obj;
	struct dma_resv *robj;
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	int attach_count;
	struct dma_buf_priv *buf = (struct dma_buf_priv *)private;
	struct priv_buf *priv_buf = buf->priv_buf;


	ret = dma_resv_lock(buf_obj->resv, NULL);
	if (ret)
		goto err;

	ret = scnprintf(priv_buf->buf + priv_buf->offset,
			priv_buf->size - priv_buf->offset,
			"%08zu\t%08x\t%08x\t%08ld\t%s\t%08lu\t%s\n",
			buf_obj->size,
			buf_obj->file->f_flags, buf_obj->file->f_mode,
			file_count(buf_obj->file),
			buf_obj->exp_name,
			file_inode(buf_obj->file)->i_ino,
			buf_obj->name ?: "");
	priv_buf->offset += ret;
	if (priv_buf->offset == priv_buf->size - 1)
		goto err;

	robj = buf_obj->resv;
	dma_resv_for_each_fence(&cursor, robj,
				DMA_RESV_USAGE_BOOKKEEP, fence) {
		if (!dma_fence_get_rcu(fence))
			continue;
		ret = scnprintf(priv_buf->buf + priv_buf->offset,
				priv_buf->size - priv_buf->offset,
				"\tFence: %s %s %ssignalled\n",
				fence->ops->get_driver_name(fence),
				fence->ops->get_timeline_name(fence),
				dma_fence_is_signaled(fence) ? "" : "un");
		priv_buf->offset += ret;
		if (priv_buf->offset == priv_buf->size - 1)
			goto err;
		dma_fence_put(fence);
	}

	ret = scnprintf(priv_buf->buf + priv_buf->offset,
			priv_buf->size - priv_buf->offset,
			"\tAttached Devices:\n");
	priv_buf->offset += ret;
	if (priv_buf->offset == priv_buf->size - 1)
		goto err;
	attach_count = 0;

	list_for_each_entry(attach_obj, &buf_obj->attachments, node) {
		ret = scnprintf(priv_buf->buf + priv_buf->offset,
				priv_buf->size - priv_buf->offset,
				"\t%s\n", dev_name(attach_obj->dev));
		priv_buf->offset += ret;
		if (priv_buf->offset == priv_buf->size - 1)
			goto err;
		attach_count++;
	}
	dma_resv_unlock(buf_obj->resv);

	ret = scnprintf(priv_buf->buf + priv_buf->offset,
			priv_buf->size - priv_buf->offset,
			"Total %d devices attached\n\n",
			attach_count);
	priv_buf->offset += ret;
	if (priv_buf->offset == priv_buf->size - 1)
		goto err;

	buf->count += 1;
	buf->size += buf_obj->size;

	return 0;
err:
	pr_err("DMABUF_INFO minidump region exhausted\n");
	return -ENOSPC;
}

void md_dma_buf_info(char *m, size_t dump_size)
{
	int ret;
	struct dma_buf_priv dma_buf_priv;
	struct priv_buf buf;

	if (!in_task())
		return;

	buf.buf = m;
	buf.size = dump_size;
	buf.offset = 0;
	dma_buf_priv.priv_buf = &buf;
	dma_buf_priv.count = 0;
	dma_buf_priv.size = 0;

	ret = scnprintf(buf.buf, buf.size, "\nDma-buf Objects:\n");
	ret += scnprintf(buf.buf + ret, buf.size - ret,
			"%-8s\t%-8s\t%-8s\t%-8s\texp_name\t%-8s\n",
			"size", "flags", "mode", "count", "ino");
	buf.offset = ret;

	dma_buf_get_each(dump_bufinfo, &dma_buf_priv);

	scnprintf(buf.buf + buf.offset, buf.size - buf.offset,
			"\nTotal %d objects, %zu bytes\n",
			dma_buf_priv.count, dma_buf_priv.size);
}

static ssize_t dma_buf_info_size_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long long  size;

	if (kstrtoull_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for size\n");
		return -EINVAL;
	}
	update_dump_size("DMA_INFO", size,
			&md_dma_buf_info_addr, &md_dma_buf_info_size);
	return count;
}

static ssize_t dma_buf_info_size_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "%llu MB\n", md_dma_buf_info_size/SZ_1M);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_dma_buf_info_size_ops = {
	.open	= simple_open,
	.write	= dma_buf_info_size_write,
	.read	= dma_buf_info_size_read,
};

void md_debugfs_dmabufinfo(struct dentry *minidump_dir)
{
	debugfs_create_file("dma_buf_info_size_mb", 0400, minidump_dir, NULL,
			    &proc_dma_buf_info_size_ops);
}

static int get_dma_info(const void *data, struct file *file, unsigned int n)
{
	struct priv_buf *buf;
	struct dma_buf_priv *dma_buf_priv;
	struct dma_buf *dmabuf;
	struct task_struct *task;
	int ret;
	u32 index;

	if (!qcom_is_dma_buf_file(file))
		return 0;

	dma_buf_priv = (struct dma_buf_priv *)data;
	buf = dma_buf_priv->priv_buf;
	task = dma_buf_priv->task;
	if (dma_buf_priv->count == 0) {
		ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
				"\n%s (PID %d)\nDMA Buffers:\n",
				task->comm, task->tgid);
		buf->offset += ret;
		if (buf->offset == buf->size - 1)
			return -EINVAL;
	}
	dmabuf = (struct dma_buf *)file->private_data;
	index = jhash(dmabuf, sizeof(struct dma_buf), DMA_BUF_HASH_SEED);
	index = index  & (DMA_BUF_HASH_SIZE - 1);
	if (dma_buf_hash[index])
		return 0;
	dma_buf_hash[index] = true;
	dma_buf_priv->count += 1;
	ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
			"%-8s\t%-8s\t%-8s\t%-8s\texp_name\t%-8s\n",
			"size", "flags", "mode", "count", "ino");
	buf->offset += ret;
	if (buf->offset == buf->size - 1)
		return -EINVAL;
	ret = scnprintf(buf->buf + buf->offset, buf->size - buf->offset,
			"%08zu\t%08x\t%08x\t%08ld\t%s\t%08lu\t%s\n",
			dmabuf->size,
			dmabuf->file->f_flags, dmabuf->file->f_mode,
			file_count(dmabuf->file),
			dmabuf->exp_name,
			file_inode(dmabuf->file)->i_ino,
			dmabuf->name ?: "");
	buf->offset += ret;
	if (buf->offset == buf->size - 1)
		return -EINVAL;
	dma_buf_priv->size += dmabuf->size;
	return 0;
}

void md_dma_buf_procs(char *m, size_t dump_size)
{
	struct task_struct *task, *thread;
	struct files_struct *files;
	int ret = 0;
	struct priv_buf buf;
	struct dma_buf_priv dma_buf_priv;

	buf.buf = m;
	buf.size = dump_size;
	buf.offset = 0;
	dma_buf_priv.priv_buf = &buf;
	dma_buf_priv.count = 0;
	dma_buf_priv.size = 0;

	rcu_read_lock();
	for_each_process(task) {
		struct files_struct *group_leader_files = NULL;

		dma_buf_priv.task = task;
		for_each_thread(task, thread) {
			task_lock(thread);
			if (unlikely(!group_leader_files))
				group_leader_files = task->group_leader->files;
			files = thread->files;
			if (files && (group_leader_files != files ||
				      thread == task->group_leader))
				ret = iterate_fd(files, 0, get_dma_info, &dma_buf_priv);
			task_unlock(thread);
			if (ret)
				goto err;
		}
		if (dma_buf_priv.count) {
			ret = scnprintf(buf.buf + buf.offset, buf.size - buf.offset,
				"\nTotal %d objects, %zu bytes\n",
				dma_buf_priv.count, dma_buf_priv.size);
			buf.offset += ret;
			if (buf.offset == buf.size - 1)
				goto err;
			dma_buf_priv.count = 0;
			dma_buf_priv.size = 0;
			memset(dma_buf_hash, 0, sizeof(dma_buf_hash));
		}
	}
	rcu_read_unlock();
	return;
err:
	rcu_read_unlock();
	pr_err("DMABUF_PROCS Minidump region exhausted\n");
}

static ssize_t dma_buf_procs_size_write(struct file *file,
					  const char __user *ubuf,
					  size_t count, loff_t *offset)
{
	unsigned long long  size;

	if (kstrtoull_from_user(ubuf, count, 0, &size)) {
		pr_err_ratelimited("Invalid format for size\n");
		return -EINVAL;
	}
	update_dump_size("DMA_PROC", size,
			&md_dma_buf_procs_addr, &md_dma_buf_procs_size);
	return count;
}

static ssize_t dma_buf_procs_size_read(struct file *file, char __user *ubuf,
				       size_t count, loff_t *offset)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "%llu MB\n", md_dma_buf_procs_size/SZ_1M);
	return simple_read_from_buffer(ubuf, count, offset, buf, strlen(buf));
}

static const struct file_operations proc_dma_buf_procs_size_ops = {
	.open	= simple_open,
	.write	= dma_buf_procs_size_write,
	.read	= dma_buf_procs_size_read,
};

void md_debugfs_dmabufprocs(struct dentry *minidump_dir)
{
	debugfs_create_file("dma_buf_procs_size_mb", 0400, minidump_dir, NULL,
			&proc_dma_buf_procs_size_ops);
}

#define MD_DEBUG_LOOKUP(_var, type) \
	do { \
		md_debug_##_var = (type *)DEBUG_SYMBOL_LOOKUP(_var); \
		if (!md_debug_##_var) { \
			pr_err("minidump: %s symbol not available in vmlinux\n", #_var); \
			error |= 1; \
		} \
	} while (0)

int md_minidump_memory_init(void)
{
	int error = 0;

	MD_DEBUG_LOOKUP(totalcma_pages, unsigned long);
	MD_DEBUG_LOOKUP(slab_caches, struct list_head);
	MD_DEBUG_LOOKUP(slab_mutex, struct mutex);
	MD_DEBUG_LOOKUP(page_owner_inited, struct static_key);
	MD_DEBUG_LOOKUP(slub_debug_enabled, struct static_key);
	MD_DEBUG_LOOKUP(min_low_pfn, unsigned long);
	MD_DEBUG_LOOKUP(max_pfn, unsigned long);

	return error;
}
