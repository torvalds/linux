// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Rockchip Electronics Co., Ltd */

#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/of_platform.h>
#include "dev.h"
#include "common.h"

int rkcif_alloc_buffer(struct rkcif_device *dev,
		       struct rkcif_dummy_buffer *buf)
{
	unsigned long attrs = buf->is_need_vaddr ? 0 : DMA_ATTR_NO_KERNEL_MAPPING;
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;
	struct sg_table	 *sg_tbl;
	void *mem_priv;
	int ret = 0;

	if (!buf->size) {
		ret = -EINVAL;
		goto err;
	}

	if (dev->hw_dev->is_dma_contig)
		attrs |= DMA_ATTR_FORCE_CONTIGUOUS;
	buf->size = PAGE_ALIGN(buf->size);
	mem_priv = g_ops->alloc(dev->hw_dev->dev, attrs, buf->size,
				DMA_BIDIRECTIONAL, GFP_KERNEL | GFP_DMA32);
	if (IS_ERR_OR_NULL(mem_priv)) {
		ret = -ENOMEM;
		goto err;
	}

	buf->mem_priv = mem_priv;
	if (dev->hw_dev->is_dma_sg_ops) {
		sg_tbl = (struct sg_table *)g_ops->cookie(mem_priv);
		buf->dma_addr = sg_dma_address(sg_tbl->sgl);
		g_ops->prepare(mem_priv);
	} else {
		buf->dma_addr = *((dma_addr_t *)g_ops->cookie(mem_priv));
	}
	if (buf->is_need_vaddr)
		buf->vaddr = g_ops->vaddr(mem_priv);
	if (buf->is_need_dbuf) {
		buf->dbuf = g_ops->get_dmabuf(mem_priv, O_RDWR);
		if (buf->is_need_dmafd) {
			buf->dma_fd = dma_buf_fd(buf->dbuf, O_CLOEXEC);
			if (buf->dma_fd < 0) {
				dma_buf_put(buf->dbuf);
				ret = buf->dma_fd;
				goto err;
			}
			get_dma_buf(buf->dbuf);
		}
	}
	v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
		 "%s buf:0x%x~0x%x size:%d\n", __func__,
		 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size, buf->size);
	return ret;
err:
	dev_err(dev->dev, "%s failed ret:%d\n", __func__, ret);
	return ret;
}

void rkcif_free_buffer(struct rkcif_device *dev,
			struct rkcif_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;

	if (buf && buf->mem_priv) {
		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
			 "%s buf:0x%x~0x%x\n", __func__,
			 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size);
		if (buf->dbuf)
			dma_buf_put(buf->dbuf);
		g_ops->put(buf->mem_priv);
		buf->size = 0;
		buf->dbuf = NULL;
		buf->vaddr = NULL;
		buf->mem_priv = NULL;
		buf->is_need_dbuf = false;
		buf->is_need_vaddr = false;
		buf->is_need_dmafd = false;
	}
}

static int rkcif_alloc_page_dummy_buf(struct rkcif_device *dev, struct rkcif_dummy_buffer *buf)
{
	struct rkcif_hw *hw = dev->hw_dev;
	u32 i, n_pages = PAGE_ALIGN(buf->size) >> PAGE_SHIFT;
	struct page *page = NULL, **pages = NULL;
	struct sg_table *sg = NULL;
	int ret = -ENOMEM;

	page = alloc_pages(GFP_KERNEL | GFP_DMA32, 0);
	if (!page)
		goto err;

	pages = kvmalloc_array(n_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto free_page;
	for (i = 0; i < n_pages; i++)
		pages[i] = page;

	sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sg)
		goto free_pages;
	ret = sg_alloc_table_from_pages(sg, pages, n_pages, 0,
					n_pages << PAGE_SHIFT, GFP_KERNEL);
	if (ret)
		goto free_sg;

	ret = dma_map_sg(hw->dev, sg->sgl, sg->nents, DMA_BIDIRECTIONAL);
	buf->dma_addr = sg_dma_address(sg->sgl);
	buf->mem_priv = sg;
	buf->pages = pages;
	v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
		 "%s buf:0x%x map cnt:%d size:%d\n", __func__,
		 (u32)buf->dma_addr, ret, buf->size);
	return 0;
free_sg:
	kfree(sg);
free_pages:
	kvfree(pages);
free_page:
	__free_pages(page, 0);
err:
	return ret;
}

static void rkcif_free_page_dummy_buf(struct rkcif_device *dev, struct rkcif_dummy_buffer *buf)
{
	struct sg_table *sg = buf->mem_priv;

	if (!sg)
		return;
	dma_unmap_sg(dev->hw_dev->dev, sg->sgl, sg->nents, DMA_BIDIRECTIONAL);
	sg_free_table(sg);
	kfree(sg);
	__free_pages(buf->pages[0], 0);
	kvfree(buf->pages);
	buf->mem_priv = NULL;
	buf->pages = NULL;
}

int rkcif_alloc_common_dummy_buf(struct rkcif_device *dev, struct rkcif_dummy_buffer *buf)
{
	struct rkcif_hw *hw = dev->hw_dev;
	int ret = 0;

	mutex_lock(&hw->dev_lock);
	if (buf->mem_priv)
		goto end;

	if (buf->size == 0)
		goto end;

	if (hw->iommu_en) {
		ret = rkcif_alloc_page_dummy_buf(dev, buf);
		goto end;
	}

	ret = rkcif_alloc_buffer(dev, buf);
	if (!ret)
		v4l2_dbg(1, rkcif_debug, &dev->v4l2_dev,
			 "%s buf:0x%x size:%d\n", __func__,
			 (u32)buf->dma_addr, buf->size);
end:
	if (ret < 0)
		v4l2_err(&dev->v4l2_dev, "%s failed:%d\n", __func__, ret);
	mutex_unlock(&hw->dev_lock);
	return ret;
}

void rkcif_free_common_dummy_buf(struct rkcif_device *dev, struct rkcif_dummy_buffer *buf)
{
	struct rkcif_hw *hw = dev->hw_dev;

	mutex_lock(&hw->dev_lock);

	if (hw->iommu_en)
		rkcif_free_page_dummy_buf(dev, buf);
	else
		rkcif_free_buffer(dev, buf);
	mutex_unlock(&hw->dev_lock);
}

