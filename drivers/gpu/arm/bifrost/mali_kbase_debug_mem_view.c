// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2013-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
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
 */

/*
 * Debugfs interface to dump the memory visible to the GPU
 */

#include "mali_kbase_debug_mem_view.h"
#include "mali_kbase.h"

#include <linux/list.h>
#include <linux/file.h>

#if IS_ENABLED(CONFIG_DEBUG_FS)

struct debug_mem_mapping {
	struct list_head node;

	struct kbase_mem_phy_alloc *alloc;
	unsigned long flags;

	u64 start_pfn;
	size_t nr_pages;
};

struct debug_mem_data {
	struct list_head mapping_list;
	struct kbase_context *kctx;
};

struct debug_mem_seq_off {
	struct list_head *lh;
	size_t offset;
};

static void *debug_mem_start(struct seq_file *m, loff_t *_pos)
{
	struct debug_mem_data *mem_data = m->private;
	struct debug_mem_seq_off *data;
	struct debug_mem_mapping *map;
	loff_t pos = *_pos;

	list_for_each_entry(map, &mem_data->mapping_list, node) {
		if (pos >= map->nr_pages) {
			pos -= map->nr_pages;
		} else {
			data = kmalloc(sizeof(*data), GFP_KERNEL);
			if (!data)
				return NULL;
			data->lh = &map->node;
			data->offset = pos;
			return data;
		}
	}

	/* Beyond the end */
	return NULL;
}

static void debug_mem_stop(struct seq_file *m, void *v)
{
	kfree(v);
}

static void *debug_mem_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct debug_mem_data *mem_data = m->private;
	struct debug_mem_seq_off *data = v;
	struct debug_mem_mapping *map;

	map = list_entry(data->lh, struct debug_mem_mapping, node);

	if (data->offset < map->nr_pages - 1) {
		data->offset++;
		++*pos;
		return data;
	}

	if (list_is_last(data->lh, &mem_data->mapping_list)) {
		kfree(data);
		return NULL;
	}

	data->lh = data->lh->next;
	data->offset = 0;
	++*pos;

	return data;
}

static int debug_mem_show(struct seq_file *m, void *v)
{
	struct debug_mem_data *mem_data = m->private;
	struct debug_mem_seq_off *data = v;
	struct debug_mem_mapping *map;
	int i, j;
	struct page *page;
	uint32_t *mapping;
	pgprot_t prot = PAGE_KERNEL;

	map = list_entry(data->lh, struct debug_mem_mapping, node);

	kbase_gpu_vm_lock(mem_data->kctx);

	if (data->offset >= map->alloc->nents) {
		seq_printf(m, "%016llx: Unbacked page\n\n", (map->start_pfn +
				data->offset) << PAGE_SHIFT);
		goto out;
	}

	if (!(map->flags & KBASE_REG_CPU_CACHED))
		prot = pgprot_writecombine(prot);

	page = as_page(map->alloc->pages[data->offset]);
	mapping = vmap(&page, 1, VM_MAP, prot);
	if (!mapping)
		goto out;

	for (i = 0; i < PAGE_SIZE; i += 4*sizeof(*mapping)) {
		seq_printf(m, "%016llx:", i + ((map->start_pfn +
				data->offset) << PAGE_SHIFT));

		for (j = 0; j < 4*sizeof(*mapping); j += sizeof(*mapping))
			seq_printf(m, " %08x", mapping[(i+j)/sizeof(*mapping)]);
		seq_putc(m, '\n');
	}

	vunmap(mapping);

	seq_putc(m, '\n');

out:
	kbase_gpu_vm_unlock(mem_data->kctx);
	return 0;
}

static const struct seq_operations ops = {
	.start = debug_mem_start,
	.next = debug_mem_next,
	.stop = debug_mem_stop,
	.show = debug_mem_show,
};

static int debug_mem_zone_open(struct rb_root *rbtree,
						struct debug_mem_data *mem_data)
{
	int ret = 0;
	struct rb_node *p;
	struct kbase_va_region *reg;
	struct debug_mem_mapping *mapping;

	for (p = rb_first(rbtree); p; p = rb_next(p)) {
		reg = rb_entry(p, struct kbase_va_region, rblink);

		if (reg->gpu_alloc == NULL)
			/* Empty region - ignore */
			continue;

		if (reg->flags & KBASE_REG_PROTECTED) {
			/* CPU access to protected memory is forbidden - so
			 * skip this GPU virtual region.
			 */
			continue;
		}

		mapping = kmalloc(sizeof(*mapping), GFP_KERNEL);
		if (!mapping) {
			ret = -ENOMEM;
			goto out;
		}

		mapping->alloc = kbase_mem_phy_alloc_get(reg->gpu_alloc);
		mapping->start_pfn = reg->start_pfn;
		mapping->nr_pages = reg->nr_pages;
		mapping->flags = reg->flags;
		list_add_tail(&mapping->node, &mem_data->mapping_list);
	}

out:
	return ret;
}

static int debug_mem_open(struct inode *i, struct file *file)
{
	struct kbase_context *const kctx = i->i_private;
	struct debug_mem_data *mem_data;
	int ret;

	if (get_file_rcu(kctx->filp) == 0)
		return -ENOENT;

	ret = seq_open(file, &ops);
	if (ret)
		goto open_fail;

	mem_data = kmalloc(sizeof(*mem_data), GFP_KERNEL);
	if (!mem_data) {
		ret = -ENOMEM;
		goto out;
	}

	mem_data->kctx = kctx;

	INIT_LIST_HEAD(&mem_data->mapping_list);

	kbase_gpu_vm_lock(kctx);

	ret = debug_mem_zone_open(&kctx->reg_rbtree_same, mem_data);
	if (ret != 0) {
		kbase_gpu_vm_unlock(kctx);
		goto out;
	}

	ret = debug_mem_zone_open(&kctx->reg_rbtree_custom, mem_data);
	if (ret != 0) {
		kbase_gpu_vm_unlock(kctx);
		goto out;
	}

	ret = debug_mem_zone_open(&kctx->reg_rbtree_exec, mem_data);
	if (ret != 0) {
		kbase_gpu_vm_unlock(kctx);
		goto out;
	}

	kbase_gpu_vm_unlock(kctx);

	((struct seq_file *)file->private_data)->private = mem_data;

	return 0;

out:
	if (mem_data) {
		while (!list_empty(&mem_data->mapping_list)) {
			struct debug_mem_mapping *mapping;

			mapping = list_first_entry(&mem_data->mapping_list,
					struct debug_mem_mapping, node);
			kbase_mem_phy_alloc_put(mapping->alloc);
			list_del(&mapping->node);
			kfree(mapping);
		}
		kfree(mem_data);
	}
	seq_release(i, file);
open_fail:
	fput(kctx->filp);

	return ret;
}

static int debug_mem_release(struct inode *inode, struct file *file)
{
	struct kbase_context *const kctx = inode->i_private;
	struct seq_file *sfile = file->private_data;
	struct debug_mem_data *mem_data = sfile->private;
	struct debug_mem_mapping *mapping;

	seq_release(inode, file);

	while (!list_empty(&mem_data->mapping_list)) {
		mapping = list_first_entry(&mem_data->mapping_list,
				struct debug_mem_mapping, node);
		kbase_mem_phy_alloc_put(mapping->alloc);
		list_del(&mapping->node);
		kfree(mapping);
	}

	kfree(mem_data);

	fput(kctx->filp);

	return 0;
}

static const struct file_operations kbase_debug_mem_view_fops = {
	.owner = THIS_MODULE,
	.open = debug_mem_open,
	.release = debug_mem_release,
	.read = seq_read,
	.llseek = seq_lseek
};

void kbase_debug_mem_view_init(struct kbase_context *const kctx)
{
	/* Caller already ensures this, but we keep the pattern for
	 * maintenance safety.
	 */
	if (WARN_ON(!kctx) ||
		WARN_ON(IS_ERR_OR_NULL(kctx->kctx_dentry)))
		return;

	debugfs_create_file("mem_view", 0400, kctx->kctx_dentry, kctx,
			&kbase_debug_mem_view_fops);
}

#endif
