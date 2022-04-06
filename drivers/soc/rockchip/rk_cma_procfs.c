// SPDX-License-Identifier: GPL-2.0
/*
 * CMA ProcFS Interface
 *
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/cma.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "../../../mm/cma.h"

static void cma_procfs_format_array(char *buf, size_t bufsize, u32 *array, int array_size)
{
	int i = 0;

	while (--array_size >= 0) {
		size_t len;
		char term = (array_size && (++i % 8)) ? ' ' : '\n';

		len = snprintf(buf, bufsize, "%08X%c", *array++, term);
		buf += len;
		bufsize -= len;
	}
}

static void cma_procfs_show_bitmap(struct seq_file *s, struct cma *cma)
{
	int elements = DIV_ROUND_UP(cma_bitmap_maxno(cma), BITS_PER_BYTE * sizeof(u32));
	int size = elements * 9;
	u32 *array = (u32 *)cma->bitmap;
	char *buf;

	buf = kmalloc(size + 1, GFP_KERNEL);
	if (!buf)
		return;

	buf[size] = 0;

	cma_procfs_format_array(buf, size + 1, array, elements);
	seq_printf(s, "%s", buf);
	kfree(buf);
}

static u64 cma_procfs_used_get(struct cma *cma)
{
	unsigned long used;

	mutex_lock(&cma->lock);
	used = bitmap_weight(cma->bitmap, (int)cma_bitmap_maxno(cma));
	mutex_unlock(&cma->lock);

	return (u64)used << cma->order_per_bit;
}

static int cma_procfs_show(struct seq_file *s, void *private)
{
	struct cma *cma = s->private;
	u64 used = cma_procfs_used_get(cma);

	seq_printf(s, "Total: %lu KiB\n", cma->count << (PAGE_SHIFT - 10));
	seq_printf(s, " Used: %llu KiB\n\n", used << (PAGE_SHIFT - 10));

	cma_procfs_show_bitmap(s, cma);

	return 0;
}

static int cma_procfs_add_one(struct cma *cma, void *data)
{
	struct proc_dir_entry *root = data;

	proc_create_single_data(cma->name, 0, root, cma_procfs_show, cma);

	return 0;
}

static int rk_cma_procfs_init(void)
{
	struct proc_dir_entry *root = proc_mkdir("rk_cma", NULL);

	return cma_for_each_area(cma_procfs_add_one, (void *)root);
}
late_initcall_sync(rk_cma_procfs_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jianqun Xu <jay.xu@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP CMA PROCFS Driver");
MODULE_ALIAS("platform:rk-cma");
