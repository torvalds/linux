// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Rockchip Electronics Co., Ltd */

#include <media/videobuf2-dma-contig.h>
#include <linux/of_platform.h>
#include "dev.h"
#include "isp_ispp.h"
#include "regs.h"

static const struct vb2_mem_ops *g_ops = &vb2_dma_contig_memops;

void rkisp_write(struct rkisp_device *dev, u32 reg, u32 val, bool is_direct)
{
	u32 *mem = dev->sw_base_addr + reg;
	u32 *flag = dev->sw_base_addr + reg + RKISP_ISP_SW_REG_SIZE;

	*mem = val;
	*flag = SW_REG_CACHE;
	if (dev->hw_dev->is_single || is_direct) {
		*flag = SW_REG_CACHE_SYNC;
		writel(val, dev->hw_dev->base_addr + reg);
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

void rkisp_set_bits(struct rkisp_device *dev, u32 reg, u32 mask, u32 val, bool is_direct)
{
	u32 tmp = rkisp_read(dev, reg, is_direct) & ~mask;

	rkisp_write(dev, reg, val | tmp, is_direct);
}

void rkisp_clear_bits(struct rkisp_device *dev, u32 reg, u32 mask, bool is_direct)
{
	u32 tmp = rkisp_read(dev, reg, is_direct);

	rkisp_write(dev, reg, tmp & ~mask, is_direct);
}

void rkisp_update_regs(struct rkisp_device *dev, u32 start, u32 end)
{
	void __iomem *base = dev->hw_dev->base_addr;
	u32 i;

	if (end > RKISP_ISP_SW_REG_SIZE - 4) {
		dev_err(dev->dev, "%s out of range\n", __func__);
		return;
	}
	for (i = start; i <= end; i += 4) {
		u32 *val = dev->sw_base_addr + i;
		u32 *flag = dev->sw_base_addr + i + RKISP_ISP_SW_REG_SIZE;

		if (*flag == SW_REG_CACHE)
			writel(*val, base + i);
	}
}

int rkisp_alloc_buffer(struct rkisp_device *dev,
		       struct rkisp_dummy_buffer *buf)
{
	unsigned long attrs = buf->is_need_vaddr ? 0 : DMA_ATTR_NO_KERNEL_MAPPING;
	void *mem_priv;
	int ret = 0;

	if (!buf->size) {
		ret = -EINVAL;
		goto err;
	}

	mem_priv = g_ops->alloc(dev->hw_dev->dev, attrs, buf->size,
				DMA_BIDIRECTIONAL, GFP_KERNEL);
	if (IS_ERR_OR_NULL(mem_priv)) {
		ret = -ENOMEM;
		goto err;
	}

	buf->mem_priv = mem_priv;
	buf->dma_addr = *((dma_addr_t *)g_ops->cookie(mem_priv));
	if (!attrs)
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
	return ret;
err:
	dev_err(dev->dev, "%s failed ret:%d\n", __func__, ret);
	return ret;
}

void rkisp_free_buffer(struct rkisp_device *dev,
		       struct rkisp_dummy_buffer *buf)
{
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

	if (hw->dev_num)
		hw->is_single = false;
	isp->dev_id = hw->dev_num;
	hw->isp[hw->dev_num] = isp;
	hw->dev_num++;
	isp->hw_dev = hw;
	isp->isp_ver = hw->isp_ver;
	isp->base_addr = hw->base_addr;

	return 0;
}
