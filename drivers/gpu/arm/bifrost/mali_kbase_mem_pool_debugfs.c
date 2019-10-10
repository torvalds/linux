/*
 *
 * (C) COPYRIGHT 2014-2019 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "mali_kbase_mem_pool_debugfs.h"
#include "mali_kbase_debugfs_helper.h"

void kbase_mem_pool_debugfs_trim(void *const array, size_t const index,
	size_t const value)
{
	struct kbase_mem_pool *const mem_pools = array;

	if (WARN_ON(!mem_pools) ||
		WARN_ON(index >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return;

	kbase_mem_pool_trim(&mem_pools[index], value);
}

void kbase_mem_pool_debugfs_set_max_size(void *const array,
	size_t const index, size_t const value)
{
	struct kbase_mem_pool *const mem_pools = array;

	if (WARN_ON(!mem_pools) ||
		WARN_ON(index >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return;

	kbase_mem_pool_set_max_size(&mem_pools[index], value);
}

size_t kbase_mem_pool_debugfs_size(void *const array, size_t const index)
{
	struct kbase_mem_pool *const mem_pools = array;

	if (WARN_ON(!mem_pools) ||
		WARN_ON(index >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return 0;

	return kbase_mem_pool_size(&mem_pools[index]);
}

size_t kbase_mem_pool_debugfs_max_size(void *const array, size_t const index)
{
	struct kbase_mem_pool *const mem_pools = array;

	if (WARN_ON(!mem_pools) ||
		WARN_ON(index >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return 0;

	return kbase_mem_pool_max_size(&mem_pools[index]);
}

void kbase_mem_pool_config_debugfs_set_max_size(void *const array,
	size_t const index, size_t const value)
{
	struct kbase_mem_pool_config *const configs = array;

	if (WARN_ON(!configs) ||
		WARN_ON(index >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return;

	kbase_mem_pool_config_set_max_size(&configs[index], value);
}

size_t kbase_mem_pool_config_debugfs_max_size(void *const array,
	size_t const index)
{
	struct kbase_mem_pool_config *const configs = array;

	if (WARN_ON(!configs) ||
		WARN_ON(index >= MEMORY_GROUP_MANAGER_NR_GROUPS))
		return 0;

	return kbase_mem_pool_config_get_max_size(&configs[index]);
}

static int kbase_mem_pool_debugfs_size_show(struct seq_file *sfile, void *data)
{
	CSTD_UNUSED(data);
	return kbase_debugfs_helper_seq_read(sfile,
		MEMORY_GROUP_MANAGER_NR_GROUPS, kbase_mem_pool_debugfs_size);
}

static ssize_t kbase_mem_pool_debugfs_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	int err;

	CSTD_UNUSED(ppos);
	err = kbase_debugfs_helper_seq_write(file, ubuf, count,
		MEMORY_GROUP_MANAGER_NR_GROUPS, kbase_mem_pool_debugfs_trim);
	return err ? err : count;
}

static int kbase_mem_pool_debugfs_open(struct inode *in, struct file *file)
{
	return single_open(file, kbase_mem_pool_debugfs_size_show,
		in->i_private);
}

static const struct file_operations kbase_mem_pool_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = kbase_mem_pool_debugfs_open,
	.read = seq_read,
	.write = kbase_mem_pool_debugfs_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int kbase_mem_pool_debugfs_max_size_show(struct seq_file *sfile,
	void *data)
{
	CSTD_UNUSED(data);
	return kbase_debugfs_helper_seq_read(sfile,
		MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_max_size);
}

static ssize_t kbase_mem_pool_debugfs_max_size_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	int err;

	CSTD_UNUSED(ppos);
	err = kbase_debugfs_helper_seq_write(file, ubuf, count,
		MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_set_max_size);
	return err ? err : count;
}

static int kbase_mem_pool_debugfs_max_size_open(struct inode *in,
	struct file *file)
{
	return single_open(file, kbase_mem_pool_debugfs_max_size_show,
		in->i_private);
}

static const struct file_operations kbase_mem_pool_debugfs_max_size_fops = {
	.owner = THIS_MODULE,
	.open = kbase_mem_pool_debugfs_max_size_open,
	.read = seq_read,
	.write = kbase_mem_pool_debugfs_max_size_write,
	.llseek = seq_lseek,
	.release = single_release,
};

void kbase_mem_pool_debugfs_init(struct dentry *parent,
		struct kbase_context *kctx)
{
	debugfs_create_file("mem_pool_size", S_IRUGO | S_IWUSR, parent,
		&kctx->mem_pools.small, &kbase_mem_pool_debugfs_fops);

	debugfs_create_file("mem_pool_max_size", S_IRUGO | S_IWUSR, parent,
		&kctx->mem_pools.small, &kbase_mem_pool_debugfs_max_size_fops);

	debugfs_create_file("lp_mem_pool_size", S_IRUGO | S_IWUSR, parent,
		&kctx->mem_pools.large, &kbase_mem_pool_debugfs_fops);

	debugfs_create_file("lp_mem_pool_max_size", S_IRUGO | S_IWUSR, parent,
		&kctx->mem_pools.large, &kbase_mem_pool_debugfs_max_size_fops);
}
