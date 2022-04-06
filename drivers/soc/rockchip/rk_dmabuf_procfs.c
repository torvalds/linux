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
	struct dma_buf *db = (struct dma_buf *)dmabuf;
	struct dma_buf_attachment *a;
	struct seq_file *s = private;
	struct scatterlist *sg;
	struct sg_table *sgt;
	phys_addr_t end, len;
	int i;

	a = dma_buf_attach(db, dmabuf_dev);
	if (IS_ERR(a))
		return;

	sgt = dma_buf_map_attachment(a, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		dma_buf_detach(db, a);
		return;
	}

	for_each_sgtable_sg(sgt, sg, i) {
		end = sg->dma_address + sg->length - 1;
		len = sg->length;
		if (i)
			seq_printf(s, "%48s", " ");
		else
			seq_printf(s, "%-16.16s %-16.16s %10lu KiB",
				   dmabuf->name,
				   dmabuf->exp_name, K(dmabuf->size));
		seq_printf(s, "%4d: %pa..%pa (%10lu %s)\n", i,
			   &sg->dma_address, &end,
			   (len >> 10) ? (K(len)) : (unsigned long)len,
			   (len >> 10) ? "KiB" : "Bytes");
	}
	dma_buf_unmap_attachment(a, sgt, DMA_BIDIRECTIONAL);
	dma_buf_detach(db, a);
}

static void rk_dmabuf_dump_sgt(struct dma_buf *dmabuf, void *private)
{
	struct seq_file *s = private;
	struct scatterlist *sg;
	struct dma_buf_attachment *a, *t;
	phys_addr_t end, len;
	int i;

	if (list_empty(&dmabuf->attachments))
		return rk_dmabuf_dump_empty_sgt(dmabuf, s);

	list_for_each_entry_safe(a, t, &dmabuf->attachments, node) {
		if (!a->sgt)
			continue;
		for_each_sgtable_sg(a->sgt, sg, i) {
			end = sg->dma_address + sg->length - 1;
			len = sg->length;
			if (i)
				seq_printf(s, "%48s", " ");
			else
				seq_printf(s, "%-16.16s %-16.16s %10lu KiB",
					   dmabuf->name,
					   dmabuf->exp_name, K(dmabuf->size));
			seq_printf(s, "%4d: %pa..%pa (%10lu %s)\n", i,
				   &sg->dma_address, &end,
				   (len >> 10) ? (K(len)) : (unsigned long)len,
				   (len >> 10) ? "KiB" : "Bytes");
		}
		break;
	}
}

static int rk_dmabuf_cb(const struct dma_buf *dmabuf, void *private)
{
	struct seq_file *s = private;
	struct dma_buf *db = (struct dma_buf *)dmabuf;

	rk_dmabuf_dump_sgt(db, s);

	return 0;
}

static int rk_dmabuf_cb2(const struct dma_buf *dmabuf, void *private)
{
	*((unsigned long *)private) += dmabuf->size;

	return 0;
}

static int rk_dmabuf_show(struct seq_file *s, void *v)
{
	int ret;
	unsigned long total_size = 0;

	seq_printf(s, "%-16s %-16s %14s %8s\n\n",
		   "NAME", "EXPORT", "SIZE:KiB", "SGLIST");

	ret = get_each_dmabuf(rk_dmabuf_cb, s);
	if (ret)
		return ret;

	ret = get_each_dmabuf(rk_dmabuf_cb2, &total_size);
	if (ret)
		return ret;

	seq_printf(s, "Total: %lu KiB\n", K(total_size));

	return 0;
}

static int __init rk_dmabuf_init(void)
{
	struct platform_device *pdev;
	struct platform_device_info dev_info = {
		.name		= "dmabuf",
		.id		= PLATFORM_DEVID_NONE,
		.dma_mask	= DMA_BIT_MASK(64),
	};

	pdev = platform_device_register_full(&dev_info);
	dmabuf_dev = pdev ? &pdev->dev : NULL;

	proc_create_single("rk_dmabuf", 0, NULL, rk_dmabuf_show);

	return 0;
}
late_initcall_sync(rk_dmabuf_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jianqun Xu <jay.xu@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP DMABUF Driver");
MODULE_ALIAS("platform:rk-dmabuf");
