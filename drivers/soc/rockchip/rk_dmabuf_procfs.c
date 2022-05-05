// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 */

#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#define K(size) ((unsigned long)((size) >> 10))
static struct device *dmabuf_dev;

static void rk_dmabuf_dump_empty_sgt(struct dma_buf *dmabuf, void *private)
{
	struct dma_buf_attachment *a;
	struct seq_file *s = private;
	struct scatterlist *sg;
	struct sg_table *sgt;
	phys_addr_t end, len;
	int i;

	a = dma_buf_attach(dmabuf, dmabuf_dev);
	if (IS_ERR(a))
		return;

	sgt = dma_buf_map_attachment(a, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		dma_buf_detach(dmabuf, a);
		return;
	}

	for_each_sgtable_sg(sgt, sg, i) {
		end = sg->dma_address + sg->length - 1;
		len = sg->length;
		if (i)
			seq_printf(s, "%65s", " ");
		else
			seq_printf(s, "%px %-16.16s %-16.16s %10lu KiB",
				   dmabuf, dmabuf->name,
				   dmabuf->exp_name, K(dmabuf->size));
		seq_printf(s, "%4d: %pa..%pa (%10lu %s)\n", i,
			   &sg->dma_address, &end,
			   (len >> 10) ? (K(len)) : (unsigned long)len,
			   (len >> 10) ? "KiB" : "Bytes");
	}
	dma_buf_unmap_attachment(a, sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(dmabuf, a);
}

static void rk_dmabuf_dump_sgt(const struct dma_buf *dmabuf, void *private)
{
	struct seq_file *s = private;
	struct scatterlist *sg;
	struct dma_buf_attachment *a, *t;
	phys_addr_t end, len;
	int i;

	list_for_each_entry_safe(a, t, &dmabuf->attachments, node) {
		if (!a->sgt)
			continue;
		for_each_sgtable_sg(a->sgt, sg, i) {
			end = sg->dma_address + sg->length - 1;
			len = sg->length;
			if (i)
				seq_printf(s, "%65s", " ");
			else
				seq_printf(s, "%px %-16.16s %-16.16s %10lu KiB",
					   dmabuf, dmabuf->name,
					   dmabuf->exp_name, K(dmabuf->size));
			seq_printf(s, "%4d: %pa..%pa (%10lu %s)\n", i,
				   &sg->dma_address, &end,
				   (len >> 10) ? (K(len)) : (unsigned long)len,
				   (len >> 10) ? "KiB" : "Bytes");
		}
		return;
	}
	/* Try to attach and map the dmabufs without sgt. */
	if (IS_ENABLED(CONFIG_DMABUF_DEBUG_ADVANCED)) {
		struct dma_buf *dbuf = (struct dma_buf *)dmabuf;

		get_dma_buf(dbuf);
		rk_dmabuf_dump_empty_sgt(dbuf, s);
		dma_buf_put(dbuf);
	}
}

static int rk_dmabuf_cb(const struct dma_buf *dmabuf, void *private)
{
	struct seq_file *s = private;

	rk_dmabuf_dump_sgt(dmabuf, s);

	return 0;
}

static int rk_dmabuf_cb3(const struct dma_buf *dmabuf, void *private)
{
	struct seq_file *s = private;
	struct dma_buf_attachment *a, *t;

	seq_printf(s, "%px %-16.16s %-16.16s %10lu KiB",
		   dmabuf, dmabuf->name,
		   dmabuf->exp_name, K(dmabuf->size));
	list_for_each_entry_safe(a, t, &dmabuf->attachments, node) {
		seq_printf(s, " %s", dev_name(a->dev));
	}
	seq_puts(s, "\n");

	return 0;
}

static int rk_dmabuf_sgt_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%16s %-16s %-16s %14s %8s\n\n",
		   "DMABUF", "NAME", "EXPORT", "SIZE:KiB", "SGLIST");

	return get_each_dmabuf(rk_dmabuf_cb, s);
}

static int rk_dmabuf_dev_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%16s %-16s %-16s %14s %8s\n\n",
		   "DMABUF", "NAME", "EXPORT", "SIZE:KiB", "AttachedDevices");

	return get_each_dmabuf(rk_dmabuf_cb3, s);
}

static int rk_dmabuf_size_show(struct seq_file *s, void *v)
{
	seq_printf(s, "Total: %lu KiB\n", K(dma_buf_get_total_size()));

	return 0;
}

static int rk_dmabuf_peak_show(struct seq_file *s, void *v)
{
	seq_printf(s, "Peak: %lu MiB\n", K(K(dma_buf_get_peak_size())));

	return 0;
}

static ssize_t rk_dmabuf_peak_write(struct file *file,
				    const char __user *buffer,
				    size_t count, loff_t *ppos)
{
	char c;
	int rc;

	rc = get_user(c, buffer);
	if (rc)
		return rc;

	if (c != '0')
		return -EINVAL;

	dma_buf_reset_peak_size();

	return count;
}

static int rk_dmabuf_peak_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_dmabuf_peak_show, NULL);
}

static const struct proc_ops rk_dmabuf_peak_ops = {
	.proc_open	= rk_dmabuf_peak_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
	.proc_write	= rk_dmabuf_peak_write,
};

static int __init rk_dmabuf_init(void)
{
	struct platform_device *pdev;
	struct platform_device_info dev_info = {
		.name		= "dmabuf",
		.id		= PLATFORM_DEVID_NONE,
		.dma_mask	= DMA_BIT_MASK(64),
	};
	struct proc_dir_entry *root = proc_mkdir("rk_dmabuf", NULL);

	pdev = platform_device_register_full(&dev_info);
	dma_set_max_seg_size(&pdev->dev, (unsigned int)DMA_BIT_MASK(64));
	dmabuf_dev = pdev ? &pdev->dev : NULL;

	proc_create_single("sgt", 0, root, rk_dmabuf_sgt_show);
	proc_create_single("dev", 0, root, rk_dmabuf_dev_show);
	proc_create_single("size", 0, root, rk_dmabuf_size_show);
	proc_create("peak", 0644, root, &rk_dmabuf_peak_ops);

	return 0;
}
late_initcall_sync(rk_dmabuf_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jianqun Xu <jay.xu@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP DMABUF Driver");
MODULE_ALIAS("platform:rk-dmabuf");
