/*
 * Copyright (C) IBM Corporation, 2014, 2017
 * Anton Blanchard, Rashmica Gupta.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
#include <asm/machdep.h>
#include <asm/debugfs.h>

/* This enables us to keep track of the memory removed from each node. */
struct memtrace_entry {
	void *mem;
	u64 start;
	u64 size;
	u32 nid;
	struct dentry *dir;
	char name[16];
};

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

static int check_memblock_online(struct memory_block *mem, void *arg)
{
	if (mem->state != MEM_ONLINE)
		return -1;

	return 0;
}

static int change_memblock_state(struct memory_block *mem, void *arg)
{
	unsigned long state = (unsigned long)arg;

	mem->state = state;

	return 0;
}

/* called with device_hotplug_lock held */
static bool memtrace_offline_pages(u32 nid, u64 start_pfn, u64 nr_pages)
{
	u64 end_pfn = start_pfn + nr_pages - 1;

	if (walk_memory_range(start_pfn, end_pfn, NULL,
	    check_memblock_online))
		return false;

	walk_memory_range(start_pfn, end_pfn, (void *)MEM_GOING_OFFLINE,
			  change_memblock_state);

	if (offline_pages(start_pfn, nr_pages)) {
		walk_memory_range(start_pfn, end_pfn, (void *)MEM_ONLINE,
				  change_memblock_state);
		return false;
	}

	walk_memory_range(start_pfn, end_pfn, (void *)MEM_OFFLINE,
			  change_memblock_state);


	return true;
}

static u64 memtrace_alloc_node(u32 nid, u64 size)
{
	u64 start_pfn, end_pfn, nr_pages, pfn;
	u64 base_pfn;
	u64 bytes = memory_block_size_bytes();

	if (!node_spanned_pages(nid))
		return 0;

	start_pfn = node_start_pfn(nid);
	end_pfn = node_end_pfn(nid);
	nr_pages = size >> PAGE_SHIFT;

	/* Trace memory needs to be aligned to the size */
	end_pfn = round_down(end_pfn - nr_pages, nr_pages);

	lock_device_hotplug();
	for (base_pfn = end_pfn; base_pfn > start_pfn; base_pfn -= nr_pages) {
		if (memtrace_offline_pages(nid, base_pfn, nr_pages) == true) {
			/*
			 * Remove memory in memory block size chunks so that
			 * iomem resources are always split to the same size and
			 * we never try to remove memory that spans two iomem
			 * resources.
			 */
			end_pfn = base_pfn + nr_pages;
			for (pfn = base_pfn; pfn < end_pfn; pfn += bytes>> PAGE_SHIFT) {
				remove_memory(nid, pfn << PAGE_SHIFT, bytes);
			}
			unlock_device_hotplug();
			return base_pfn << PAGE_SHIFT;
		}
	}
	unlock_device_hotplug();

	return 0;
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
		if (!dir) {
			pr_err("Failed to create debugfs directory for node %d\n",
				ent->nid);
			return -1;
		}

		ent->dir = dir;
		debugfs_create_file("trace", 0400, dir, ent, &memtrace_fops);
		debugfs_create_x64("start", 0400, dir, &ent->start);
		debugfs_create_x64("size", 0400, dir, &ent->size);
	}

	return ret;
}

static int online_mem_block(struct memory_block *mem, void *arg)
{
	return device_online(&mem->dev);
}

/*
 * Iterate through the chunks of memory we have removed from the kernel
 * and attempt to add them back to the kernel.
 */
static int memtrace_online(void)
{
	int i, ret = 0;
	struct memtrace_entry *ent;

	for (i = memtrace_array_nr - 1; i >= 0; i--) {
		ent = &memtrace_array[i];

		/* We have onlined this chunk previously */
		if (ent->nid == -1)
			continue;

		/* Remove from io mappings */
		if (ent->mem) {
			iounmap(ent->mem);
			ent->mem = 0;
		}

		if (add_memory(ent->nid, ent->start, ent->size)) {
			pr_err("Failed to add trace memory to node %d\n",
				ent->nid);
			ret += 1;
			continue;
		}

		/*
		 * If kernel isn't compiled with the auto online option
		 * we need to online the memory ourselves.
		 */
		if (!memhp_auto_online) {
			lock_device_hotplug();
			walk_memory_range(PFN_DOWN(ent->start),
					  PFN_UP(ent->start + ent->size - 1),
					  NULL, online_mem_block);
			unlock_device_hotplug();
		}

		/*
		 * Memory was added successfully so clean up references to it
		 * so on reentry we can tell that this chunk was added.
		 */
		debugfs_remove_recursive(ent->dir);
		pr_info("Added trace memory back to node %d\n", ent->nid);
		ent->size = ent->start = ent->nid = -1;
	}
	if (ret)
		return ret;

	/* If all chunks of memory were added successfully, reset globals */
	kfree(memtrace_array);
	memtrace_array = NULL;
	memtrace_size = 0;
	memtrace_array_nr = 0;
	return 0;
}

static int memtrace_enable_set(void *data, u64 val)
{
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

	/* Re-add/online previously removed/offlined memory */
	if (memtrace_size) {
		if (memtrace_online())
			return -EAGAIN;
	}

	if (!val)
		return 0;

	/* Offline and remove memory */
	if (memtrace_init_regions_runtime(val))
		return -EINVAL;

	if (memtrace_init_debugfs())
		return -EINVAL;

	memtrace_size = val;

	return 0;
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
	if (!memtrace_debugfs_dir)
		return -1;

	debugfs_create_file("enable", 0600, memtrace_debugfs_dir,
			    NULL, &memtrace_init_fops);

	return 0;
}
machine_device_initcall(powernv, memtrace_init);
