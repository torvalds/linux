// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) IBM Corporation, 2014, 2017
 * Anton Blanchard, Rashmica Gupta.
 */

#define pr_fmt(fmt) "memtrace: " fmt

#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/numa.h>
#include <asm/machdep.h>
#include <asm/debugfs.h>
#include <asm/cacheflush.h>

/* This enables us to keep track of the memory removed from each node. */
struct memtrace_entry {
	void *mem;
	u64 start;
	u64 size;
	u32 nid;
	struct dentry *dir;
	char name[16];
};

static DEFINE_MUTEX(memtrace_mutex);
static u64 memtrace_size;

static struct memtrace_entry *memtrace_array;
static unsigned int memtrace_array_nr;


static ssize_t memtrace_read(struct file *filp, char __user *ubuf,
			     size_t count, loff_t *ppos)
{
	struct memtrace_entry *ent = filp->private_data;

	return simple_read_from_buffer(ubuf, count, ppos, ent->mem, ent->size);
}

static const struct file_operations memtrace_fops = {
	.llseek = default_llseek,
	.read	= memtrace_read,
	.open	= simple_open,
};

#define FLUSH_CHUNK_SIZE SZ_1G
/**
 * flush_dcache_range_chunked(): Write any modified data cache blocks out to
 * memory and invalidate them, in chunks of up to FLUSH_CHUNK_SIZE
 * Does not invalidate the corresponding instruction cache blocks.
 *
 * @start: the start address
 * @stop: the stop address (exclusive)
 * @chunk: the max size of the chunks
 */
static void flush_dcache_range_chunked(unsigned long start, unsigned long stop,
				       unsigned long chunk)
{
	unsigned long i;

	for (i = start; i < stop; i += chunk) {
		flush_dcache_range(i, min(stop, i + chunk));
		cond_resched();
	}
}

static void memtrace_clear_range(unsigned long start_pfn,
				 unsigned long nr_pages)
{
	unsigned long pfn;

	/* As HIGHMEM does not apply, use clear_page() directly. */
	for (pfn = start_pfn; pfn < start_pfn + nr_pages; pfn++) {
		if (IS_ALIGNED(pfn, PAGES_PER_SECTION))
			cond_resched();
		clear_page(__va(PFN_PHYS(pfn)));
	}
	/*
	 * Before we go ahead and use this range as cache inhibited range
	 * flush the cache.
	 */
	flush_dcache_range_chunked(PFN_PHYS(start_pfn),
				   PFN_PHYS(start_pfn + nr_pages),
				   FLUSH_CHUNK_SIZE);
}

static u64 memtrace_alloc_node(u32 nid, u64 size)
{
	const unsigned long nr_pages = PHYS_PFN(size);
	unsigned long pfn, start_pfn;
	struct page *page;

	/*
	 * Trace memory needs to be aligned to the size, which is guaranteed
	 * by alloc_contig_pages().
	 */
	page = alloc_contig_pages(nr_pages, GFP_KERNEL | __GFP_THISNODE |
				  __GFP_NOWARN, nid, NULL);
	if (!page)
		return 0;
	start_pfn = page_to_pfn(page);

	/*
	 * Clear the range while we still have a linear mapping.
	 *
	 * TODO: use __GFP_ZERO with alloc_contig_pages() once supported.
	 */
	memtrace_clear_range(start_pfn, nr_pages);

	/*
	 * Set pages PageOffline(), to indicate that nobody (e.g., hibernation,
	 * dumping, ...) should be touching these pages.
	 */
	for (pfn = start_pfn; pfn < start_pfn + nr_pages; pfn++)
		__SetPageOffline(pfn_to_page(pfn));

	arch_remove_linear_mapping(PFN_PHYS(start_pfn), size);

	return PFN_PHYS(start_pfn);
}

static int memtrace_init_regions_runtime(u64 size)
{
	u32 nid;
	u64 m;

	memtrace_array = kcalloc(num_online_nodes(),
				sizeof(struct memtrace_entry), GFP_KERNEL);
	if (!memtrace_array) {
		pr_err("Failed to allocate memtrace_array\n");
		return -EINVAL;
	}

	for_each_online_node(nid) {
		m = memtrace_alloc_node(nid, size);

		/*
		 * A node might not have any local memory, so warn but
		 * continue on.
		 */
		if (!m) {
			pr_err("Failed to allocate trace memory on node %d\n", nid);
			continue;
		}

		pr_info("Allocated trace memory on node %d at 0x%016llx\n", nid, m);

		memtrace_array[memtrace_array_nr].start = m;
		memtrace_array[memtrace_array_nr].size = size;
		memtrace_array[memtrace_array_nr].nid = nid;
		memtrace_array_nr++;
	}

	return 0;
}

static struct dentry *memtrace_debugfs_dir;

static int memtrace_init_debugfs(void)
{
	int ret = 0;
	int i;

	for (i = 0; i < memtrace_array_nr; i++) {
		struct dentry *dir;
		struct memtrace_entry *ent = &memtrace_array[i];

		ent->mem = ioremap(ent->start, ent->size);
		/* Warn but continue on */
		if (!ent->mem) {
			pr_err("Failed to map trace memory at 0x%llx\n",
				 ent->start);
			ret = -1;
			continue;
		}

		snprintf(ent->name, 16, "%08x", ent->nid);
		dir = debugfs_create_dir(ent->name, memtrace_debugfs_dir);

		ent->dir = dir;
		debugfs_create_file("trace", 0400, dir, ent, &memtrace_fops);
		debugfs_create_x64("start", 0400, dir, &ent->start);
		debugfs_create_x64("size", 0400, dir, &ent->size);
	}

	return ret;
}

static int memtrace_free(int nid, u64 start, u64 size)
{
	struct mhp_params params = { .pgprot = PAGE_KERNEL };
	const unsigned long nr_pages = PHYS_PFN(size);
	const unsigned long start_pfn = PHYS_PFN(start);
	unsigned long pfn;
	int ret;

	ret = arch_create_linear_mapping(nid, start, size, &params);
	if (ret)
		return ret;

	for (pfn = start_pfn; pfn < start_pfn + nr_pages; pfn++)
		__ClearPageOffline(pfn_to_page(pfn));

	free_contig_range(start_pfn, nr_pages);
	return 0;
}

/*
 * Iterate through the chunks of memory we allocated and attempt to expose
 * them back to the kernel.
 */
static int memtrace_free_regions(void)
{
	int i, ret = 0;
	struct memtrace_entry *ent;

	for (i = memtrace_array_nr - 1; i >= 0; i--) {
		ent = &memtrace_array[i];

		/* We have freed this chunk previously */
		if (ent->nid == NUMA_NO_NODE)
			continue;

		/* Remove from io mappings */
		if (ent->mem) {
			iounmap(ent->mem);
			ent->mem = 0;
		}

		if (memtrace_free(ent->nid, ent->start, ent->size)) {
			pr_err("Failed to free trace memory on node %d\n",
				ent->nid);
			ret += 1;
			continue;
		}

		/*
		 * Memory was freed successfully so clean up references to it
		 * so on reentry we can tell that this chunk was freed.
		 */
		debugfs_remove_recursive(ent->dir);
		pr_info("Freed trace memory back on node %d\n", ent->nid);
		ent->size = ent->start = ent->nid = NUMA_NO_NODE;
	}
	if (ret)
		return ret;

	/* If all chunks of memory were freed successfully, reset globals */
	kfree(memtrace_array);
	memtrace_array = NULL;
	memtrace_size = 0;
	memtrace_array_nr = 0;
	return 0;
}

static int memtrace_enable_set(void *data, u64 val)
{
	int rc = -EAGAIN;
	u64 bytes;

	/*
	 * Don't attempt to do anything if size isn't aligned to a memory
	 * block or equal to zero.
	 */
	bytes = memory_block_size_bytes();
	if (val & (bytes - 1)) {
		pr_err("Value must be aligned with 0x%llx\n", bytes);
		return -EINVAL;
	}

	mutex_lock(&memtrace_mutex);

	/* Free all previously allocated memory. */
	if (memtrace_size && memtrace_free_regions())
		goto out_unlock;

	if (!val) {
		rc = 0;
		goto out_unlock;
	}

	/* Allocate memory. */
	if (memtrace_init_regions_runtime(val))
		goto out_unlock;

	if (memtrace_init_debugfs())
		goto out_unlock;

	memtrace_size = val;
	rc = 0;
out_unlock:
	mutex_unlock(&memtrace_mutex);
	return rc;
}

static int memtrace_enable_get(void *data, u64 *val)
{
	*val = memtrace_size;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(memtrace_init_fops, memtrace_enable_get,
					memtrace_enable_set, "0x%016llx\n");

static int memtrace_init(void)
{
	memtrace_debugfs_dir = debugfs_create_dir("memtrace",
						  powerpc_debugfs_root);

	debugfs_create_file("enable", 0600, memtrace_debugfs_dir,
			    NULL, &memtrace_init_fops);

	return 0;
}
machine_device_initcall(powernv, memtrace_init);
