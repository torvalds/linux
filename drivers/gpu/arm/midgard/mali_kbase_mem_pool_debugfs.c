/*
 *
 * (C) COPYRIGHT 2014-2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <mali_kbase_mem_pool_debugfs.h>

#ifdef CONFIG_DEBUG_FS

static int kbase_mem_pool_debugfs_size_get(void *data, u64 *val)
{
	struct kbase_mem_pool *pool = (struct kbase_mem_pool *)data;

	*val = kbase_mem_pool_size(pool);

	return 0;
}

static int kbase_mem_pool_debugfs_size_set(void *data, u64 val)
{
	struct kbase_mem_pool *pool = (struct kbase_mem_pool *)data;

	kbase_mem_pool_trim(pool, val);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(kbase_mem_pool_debugfs_size_fops,
		kbase_mem_pool_debugfs_size_get,
		kbase_mem_pool_debugfs_size_set,
		"%llu\n");

static int kbase_mem_pool_debugfs_max_size_get(void *data, u64 *val)
{
	struct kbase_mem_pool *pool = (struct kbase_mem_pool *)data;

	*val = kbase_mem_pool_max_size(pool);

	return 0;
}

static int kbase_mem_pool_debugfs_max_size_set(void *data, u64 val)
{
	struct kbase_mem_pool *pool = (struct kbase_mem_pool *)data;

	kbase_mem_pool_set_max_size(pool, val);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(kbase_mem_pool_debugfs_max_size_fops,
		kbase_mem_pool_debugfs_max_size_get,
		kbase_mem_pool_debugfs_max_size_set,
		"%llu\n");

void kbase_mem_pool_debugfs_init(struct dentry *parent,
		struct kbase_mem_pool *pool)
{
	debugfs_create_file("mem_pool_size", S_IRUGO | S_IWUSR, parent,
			pool, &kbase_mem_pool_debugfs_size_fops);

	debugfs_create_file("mem_pool_max_size", S_IRUGO | S_IWUSR, parent,
			pool, &kbase_mem_pool_debugfs_max_size_fops);
}

#endif /* CONFIG_DEBUG_FS */
