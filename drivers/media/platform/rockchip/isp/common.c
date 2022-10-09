// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Rockchip Electronics Co., Ltd */

#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include "dev.h"
#include "isp_ispp.h"
#include "regs.h"

void rkisp_write(struct rkisp_device *dev, u32 reg, u32 val, bool is_direct)
{
	u32 *mem = dev->sw_base_addr + reg;
	u32 *flag = dev->sw_base_addr + reg + RKISP_ISP_SW_REG_SIZE;

	*mem = val;
	*flag = SW_REG_CACHE;
	if (dev->hw_dev->is_single || is_direct) {
		*flag = SW_REG_CACHE_SYNC;
		if (dev->isp_ver == ISP_V32 && reg <= 0x200)
			rv1106_sdmmc_get_lock();
		writel(val, dev->hw_dev->base_addr + reg);
		if (dev->isp_ver == ISP_V32 && reg <= 0x200)
			rv1106_sdmmc_put_lock();
	}
}

void rkisp_next_write(struct rkisp_device *dev, u32 reg, u32 val, bool is_direct)
{
	u32 offset = RKISP_ISP_SW_MAX_SIZE + reg;
	u32 *mem = dev->sw_base_addr + offset;
	u32 *flag = dev->sw_base_addr + offset + RKISP_ISP_SW_REG_SIZE;

	*mem = val;
	*flag = SW_REG_CACHE;
	if (dev->hw_dev->is_single || is_direct) {
		*flag = SW_REG_CACHE_SYNC;
		writel(val, dev->hw_dev->base_next_addr + reg);
	}
}

u32 rkisp_read(struct rkisp_device *dev, u32 reg, bool is_direct)
{
	u32 val;

	if (dev->hw_dev->is_single || is_direct)
		val = readl(dev->hw_dev->base_addr + reg);
	else
		val = *(u32 *)(dev->sw_base_addr + reg);
	return val;
}

u32 rkisp_next_read(struct rkisp_device *dev, u32 reg, bool is_direct)
{
	u32 val;

	if (dev->hw_dev->is_single || is_direct)
		val = readl(dev->hw_dev->base_next_addr + reg);
	else
		val = *(u32 *)(dev->sw_base_addr + RKISP_ISP_SW_MAX_SIZE + reg);
	return val;
}

void rkisp_set_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val, bool is_direct)
{
	u32 tmp = rkisp_read(dev, reg, is_direct) & ~mask;

	rkisp_write(dev, reg, val | tmp, is_direct);
}

void rkisp_next_set_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val, bool is_direct)
{
	u32 tmp = rkisp_next_read(dev, reg, is_direct) & ~mask;

	rkisp_next_write(dev, reg, val | tmp, is_direct);
}

void rkisp_clear_bits(struct rkisp_device *dev, u32 reg, u32 mask, bool is_direct)
{
	u32 tmp = rkisp_read(dev, reg, is_direct);

	rkisp_write(dev, reg, tmp & ~mask, is_direct);
}

void rkisp_next_clear_bits(struct rkisp_device *dev, u32 reg, u32 mask, bool is_direct)
{
	u32 tmp = rkisp_next_read(dev, reg, is_direct);

	rkisp_next_write(dev, reg, tmp & ~mask, is_direct);
}

void rkisp_write_reg_cache(struct rkisp_device *dev, u32 reg, u32 val)
{
	u32 *mem = dev->sw_base_addr + reg;

	*mem = val;
}

void rkisp_next_write_reg_cache(struct rkisp_device *dev, u32 reg, u32 val)
{
	u32 offset = RKISP_ISP_SW_MAX_SIZE + reg;
	u32 *mem = dev->sw_base_addr + offset;

	*mem = val;
}

u32 rkisp_read_reg_cache(struct rkisp_device *dev, u32 reg)
{
	return *(u32 *)(dev->sw_base_addr + reg);
}

u32 rkisp_next_read_reg_cache(struct rkisp_device *dev, u32 reg)
{
	return *(u32 *)(dev->sw_base_addr + RKISP_ISP_SW_MAX_SIZE + reg);
}

void rkisp_set_reg_cache_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val)
{
	u32 tmp = rkisp_read_reg_cache(dev, reg) & ~mask;

	rkisp_write_reg_cache(dev, reg, val | tmp);
}

void rkisp_next_set_reg_cache_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val)
{
	u32 tmp = rkisp_next_read_reg_cache(dev, reg) & ~mask;

	rkisp_next_write_reg_cache(dev, reg, val | tmp);
}

void rkisp_clear_reg_cache_bits(struct rkisp_device *dev, u32 reg, u32 mask)
{
	u32 tmp = rkisp_read_reg_cache(dev, reg);

	rkisp_write_reg_cache(dev, reg, tmp & ~mask);
}

void rkisp_next_clear_reg_cache_bits(struct rkisp_device *dev, u32 reg, u32 mask)
{
	u32 tmp = rkisp_next_read_reg_cache(dev, reg);

	rkisp_next_write_reg_cache(dev, reg, tmp & ~mask);
}

void rkisp_update_regs(struct rkisp_device *dev, u32 start, u32 end)
{
	struct rkisp_hw_dev *hw = dev->hw_dev;
	void __iomem *base = hw->base_addr;
	u32 i;

	if (end > RKISP_ISP_SW_REG_SIZE - 4) {
		dev_err(dev->dev, "%s out of range\n", __func__);
		return;
	}
	for (i = start; i <= end; i += 4) {
		u32 *val = dev->sw_base_addr + i;
		u32 *flag = dev->sw_base_addr + i + RKISP_ISP_SW_REG_SIZE;

		if (dev->procfs.mode & RKISP_PROCFS_FIL_SW) {
			if (!((i >= ISP3X_ISP_ACQ_H_OFFS && i <= ISP3X_ISP_ACQ_V_SIZE) ||
			      (i >= ISP3X_ISP_OUT_H_OFFS && i <= ISP3X_ISP_OUT_V_SIZE) ||
			      (i >= ISP3X_DUAL_CROP_BASE && i < ISP3X_DUAL_CROP_FBC_V_SIZE_SHD) ||
			      (i >= ISP3X_MAIN_RESIZE_BASE && i < ISP3X_CSI2RX_VERSION) ||
			      (i >= CIF_ISP_IS_BASE && i < CIF_ISP_IS_V_SIZE_SHD)))
				continue;
		}

		if (*flag == SW_REG_CACHE) {
			if ((i == ISP3X_MAIN_RESIZE_CTRL ||
			     i == ISP32_BP_RESIZE_CTRL ||
			     i == ISP3X_SELF_RESIZE_CTRL) && *val == 0)
				*val = CIF_RSZ_CTRL_CFG_UPD;
			writel(*val, base + i);
			if (hw->is_unite) {
				val = dev->sw_base_addr + i + RKISP_ISP_SW_MAX_SIZE;
				if ((i == ISP3X_MAIN_RESIZE_CTRL ||
				     i == ISP32_BP_RESIZE_CTRL ||
				     i == ISP3X_SELF_RESIZE_CTRL) && *val == 0)
					*val = CIF_RSZ_CTRL_CFG_UPD;
				writel(*val, hw->base_next_addr + i);
			}
		}
	}
}

int rkisp_alloc_buffer(struct rkisp_device *dev,
		       struct rkisp_dummy_buffer *buf)
{
	unsigned long attrs = buf->is_need_vaddr ? 0 : DMA_ATTR_NO_KERNEL_MAPPING;
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;
	struct sg_table	 *sg_tbl;
	void *mem_priv;
	int ret = 0;

	mutex_lock(&dev->buf_lock);

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
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s buf:0x%x~0x%x size:%d\n", __func__,
		 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size, buf->size);
	mutex_unlock(&dev->buf_lock);
	return ret;
err:
	mutex_unlock(&dev->buf_lock);
	dev_err(dev->dev, "%s failed ret:%d\n", __func__, ret);
	return ret;
}

void rkisp_free_buffer(struct rkisp_device *dev,
			struct rkisp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;

	mutex_lock(&dev->buf_lock);
	if (buf && buf->mem_priv) {
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
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
	mutex_unlock(&dev->buf_lock);
}

void rkisp_prepare_buffer(struct rkisp_device *dev,
			struct rkisp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;

	if (buf && buf->mem_priv)
		g_ops->prepare(buf->mem_priv);
}

void rkisp_finish_buffer(struct rkisp_device *dev,
			struct rkisp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;

	if (buf && buf->mem_priv)
		g_ops->finish(buf->mem_priv);
}

int rkisp_attach_hw(struct rkisp_device *isp)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct rkisp_hw_dev *hw;

	np = of_parse_phandle(isp->dev->of_node, "rockchip,hw", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(isp->dev, "failed to get isp hw node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_err(isp->dev, "failed to get isp hw from node\n");
		return -ENODEV;
	}

	hw = platform_get_drvdata(pdev);
	if (!hw) {
		dev_err(isp->dev, "failed attach isp hw\n");
		return -EINVAL;
	}

	isp->dev_id = hw->dev_num;
	hw->isp[hw->dev_num] = isp;
	hw->dev_num++;
	isp->hw_dev = hw;
	isp->isp_ver = hw->isp_ver;
	isp->base_addr = hw->base_addr;

	return 0;
}

static int rkisp_alloc_page_dummy_buf(struct rkisp_device *dev, u32 size)
{
	struct rkisp_hw_dev *hw = dev->hw_dev;
	struct rkisp_dummy_buffer *dummy_buf = &hw->dummy_buf;
	u32 i, n_pages = PAGE_ALIGN(size) >> PAGE_SHIFT;
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
	dummy_buf->dma_addr = sg_dma_address(sg->sgl);
	dummy_buf->mem_priv = sg;
	dummy_buf->pages = pages;
	v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
		 "%s buf:0x%x map cnt:%d size:%d\n", __func__,
		 (u32)dummy_buf->dma_addr, ret, size);
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

static void rkisp_free_page_dummy_buf(struct rkisp_device *dev)
{
	struct rkisp_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
	struct sg_table *sg = dummy_buf->mem_priv;

	if (!sg)
		return;
	dma_unmap_sg(dev->hw_dev->dev, sg->sgl, sg->nents, DMA_BIDIRECTIONAL);
	sg_free_table(sg);
	kfree(sg);
	__free_pages(dummy_buf->pages[0], 0);
	kvfree(dummy_buf->pages);
	dummy_buf->mem_priv = NULL;
	dummy_buf->pages = NULL;
}

int rkisp_alloc_common_dummy_buf(struct rkisp_device *dev)
{
	struct rkisp_hw_dev *hw = dev->hw_dev;
	struct rkisp_dummy_buffer *dummy_buf = &hw->dummy_buf;
	struct rkisp_stream *stream;
	struct rkisp_device *isp;
	u32 i, j, val, size = 0;
	int ret = 0;

	if (dummy_buf->mem_priv)
		goto end;

	if (hw->max_in.w && hw->max_in.h)
		size = hw->max_in.w * hw->max_in.h * 2;
	for (i = 0; i < hw->dev_num; i++) {
		isp = hw->isp[i];
		if (!isp || (isp && !isp->is_hw_link))
			continue;
		for (j = 0; j < RKISP_MAX_STREAM; j++) {
			stream = &isp->cap_dev.stream[j];
			if (!stream->linked)
				continue;
			val = stream->out_isp_fmt.fmt_type == FMT_FBC ?
				stream->out_fmt.plane_fmt[1].sizeimage :
				stream->out_fmt.plane_fmt[0].bytesperline *
				stream->out_fmt.height;
			size = max(size, val);
		}
	}
	if (size == 0)
		goto end;

	if (hw->is_mmu) {
		ret = rkisp_alloc_page_dummy_buf(dev, size);
		goto end;
	}

	dummy_buf->size = size;
	ret = rkisp_alloc_buffer(dev, dummy_buf);
	if (!ret)
		v4l2_dbg(1, rkisp_debug, &dev->v4l2_dev,
			 "%s buf:0x%x size:%d\n", __func__,
			 (u32)dummy_buf->dma_addr, dummy_buf->size);
end:
	if (ret < 0)
		v4l2_err(&dev->v4l2_dev, "%s failed:%d\n", __func__, ret);
	return ret;
}

void rkisp_free_common_dummy_buf(struct rkisp_device *dev)
{
	struct rkisp_hw_dev *hw = dev->hw_dev;

	if (atomic_read(&hw->refcnt) ||
	    atomic_read(&dev->cap_dev.refcnt) > 1)
		return;

	if (hw->is_mmu)
		rkisp_free_page_dummy_buf(dev);
	else
		rkisp_free_buffer(dev, &hw->dummy_buf);
}
