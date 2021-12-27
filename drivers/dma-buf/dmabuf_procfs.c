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

static void rk_dmabuf_dump_sgt(struct dma_buf *dmabuf, void *priv)
{
	struct seq_file *s = priv;
	struct scatterlist *sg;
	struct dma_buf_attachment *a;
	phys_addr_t end;
	int i;

	list_for_each_entry(a, &dmabuf->attachments, node) {
		if (!a->sgt)
			continue;
		for_each_sgtable_sg(a->sgt, sg, i) {
			end = sg->dma_address + sg->length - 1;
			seq_printf(s, "%4d: ", i);
			seq_printf(s, "%pa..%pa\t", &sg->dma_address, &end);
		}
		break;
	}
}

static int rk_dmabuf_cb(const struct dma_buf *dmabuf, void *private)
{
	struct seq_file *s = private;
	struct sg_table *sgt;
	struct dma_buf_attachment *a;
	struct dma_buf *db = (struct dma_buf *)dmabuf;
	bool need_map = list_empty(&db->attachments);

	seq_printf(s, "%s\t%32s %10lu KiB", db->name, db->exp_name, K(db->size));

	if (need_map) {
		a = dma_buf_attach(db, dmabuf_dev);
		if (IS_ERR_OR_NULL(a))
			return PTR_ERR(a);

		sgt = dma_buf_map_attachment(a, 0);
		if (IS_ERR_OR_NULL(sgt))
			return PTR_ERR(sgt);
	}

	rk_dmabuf_dump_sgt(db, s);

	if (need_map) {
		dma_buf_unmap_attachment(a, sgt, 0);
		dma_buf_detach(db, a);
	}

	seq_puts(s, "\n");

	return 0;
}

static int rk_dmabuf_show(struct seq_file *s, void *v)
{
	seq_printf(s, "%s\t%32s %14s %8s\n\n", "NAME", "EXPORT", "SIZE:KiB", "SGLIST");

	return get_each_dmabuf(rk_dmabuf_cb, s);
}

static int __init rk_dmabuf_init(void)
{
	struct platform_device *pdev;
	struct platform_device_info dev_info = {
		.name		= "dmabuf",
		.id		= PLATFORM_DEVID_AUTO,
		.dma_mask	= DMA_BIT_MASK(32),
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
