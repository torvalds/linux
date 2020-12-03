// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Rockchip Electronics Co., Ltd */

#include <media/videobuf2-dma-contig.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/of_platform.h>
#include "dev.h"
#include "regs.h"

static const struct vb2_mem_ops *g_ops = &vb2_dma_contig_memops;

void rkispp_write(struct rkispp_device *dev, u32 reg, u32 val)
{
	u32 *mem = dev->sw_base_addr + reg;
	u32 *flag = dev->sw_base_addr + reg + ISPP_SW_REG_SIZE;

	*mem = val;
	*flag = SW_REG_CACHE;
	if (dev->hw_dev->is_single)
		writel(val, dev->hw_dev->base_addr + reg);
}

u32 rkispp_read(struct rkispp_device *dev, u32 reg)
{
	u32 val;

	if (dev->hw_dev->is_single)
		val = readl(dev->hw_dev->base_addr + reg);
	else
		val = *(u32 *)(dev->sw_base_addr + reg);
	return val;
}

void rkispp_set_bits(struct rkispp_device *dev, u32 reg, u32 mask, u32 val)
{
	u32 tmp = rkispp_read(dev, reg) & ~mask;

	rkispp_write(dev, reg, val | tmp);
}

void rkispp_clear_bits(struct rkispp_device *dev, u32 reg, u32 mask)
{
	u32 tmp = rkispp_read(dev, reg);

	rkispp_write(dev, reg, tmp & ~mask);
}

void rkispp_update_regs(struct rkispp_device *dev, u32 start, u32 end)
{
	void __iomem *base = dev->hw_dev->base_addr;
	u32 i;

	if (end > ISPP_SW_REG_SIZE - 4) {
		dev_err(dev->dev, "%s out of range\n", __func__);
		return;
	}
	for (i = start; i <= end; i += 4) {
		u32 *val = dev->sw_base_addr + i;
		u32 *flag = dev->sw_base_addr + i + ISPP_SW_REG_SIZE;

		if (*flag == SW_REG_CACHE)
			writel(*val, base + i);
	}
}

int rkispp_allow_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf)
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
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s buf:0x%x~0x%x size:%d\n", __func__,
		 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size, buf->size);
	return ret;
err:
	dev_err(dev->dev, "%s failed ret:%d\n", __func__, ret);
	return ret;
}

void rkispp_free_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf)
{
	if (buf && buf->mem_priv) {
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
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

int rkispp_attach_hw(struct rkispp_device *ispp)
{
	struct device_node *np;
	struct platform_device *pdev;
	struct rkispp_hw_dev *hw;

	np = of_parse_phandle(ispp->dev->of_node, "rockchip,hw", 0);
	if (!np || !of_device_is_available(np)) {
		dev_err(ispp->dev, "failed to get ispp hw node\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev) {
		dev_err(ispp->dev, "failed to get ispp hw from node\n");
		return -ENODEV;
	}

	hw = platform_get_drvdata(pdev);
	if (!hw) {
		dev_err(ispp->dev, "failed attach ispp hw\n");
		return -EINVAL;
	}

	if (hw->dev_num)
		hw->is_single = false;
	ispp->dev_id = hw->dev_num;
	hw->ispp[hw->dev_num] = ispp;
	hw->dev_num++;
	ispp->hw_dev = hw;
	ispp->ispp_ver = hw->ispp_ver;

	return 0;
}

static void rkispp_free_pool(struct rkispp_hw_dev *hw)
{
	struct rkispp_isp_buf_pool *buf;
	int i, j;

	if (atomic_read(&hw->refcnt))
		return;

	for (i = 0; i < RKISPP_BUF_POOL_MAX; i++) {
		buf = &hw->pool[i];
		if (!buf->dbufs)
			break;
		if (rkispp_debug)
			dev_info(hw->dev, "%s dbufs[%d]:0x%p\n",
				 __func__, i, buf->dbufs);
		for (j = 0; j < GROUP_BUF_MAX; j++) {
			if (buf->mem_priv[j]) {
				g_ops->unmap_dmabuf(buf->mem_priv[j]);
				g_ops->detach_dmabuf(buf->mem_priv[j]);
				buf->mem_priv[j] = NULL;
			}
		}
		buf->dbufs = NULL;
	}
}

static int rkispp_init_pool(struct rkispp_hw_dev *hw, struct rkisp_ispp_buf *dbufs)
{
	struct rkispp_isp_buf_pool *pool;
	int i, ret = 0;
	void *mem;

	INIT_LIST_HEAD(&hw->list);
	/* init dma buf pool */
	for (i = 0; i < RKISPP_BUF_POOL_MAX; i++) {
		pool = &hw->pool[i];
		if (!pool->dbufs)
			break;
	}
	dbufs->is_isp = true;
	pool->dbufs = dbufs;
	if (rkispp_debug)
		dev_info(hw->dev, "%s dbufs[%d]:0x%p\n",
			 __func__, i, dbufs);
	for (i = 0; i < GROUP_BUF_MAX; i++) {
		mem = g_ops->attach_dmabuf(hw->dev, dbufs->dbuf[i],
			dbufs->dbuf[i]->size, DMA_BIDIRECTIONAL);
		if (IS_ERR(mem)) {
			ret = PTR_ERR(mem);
			goto err;
		}
		pool->mem_priv[i] = mem;
		ret = g_ops->map_dmabuf(mem);
		if (ret)
			goto err;
		pool->dma[i] = *((dma_addr_t *)g_ops->cookie(mem));
		if (rkispp_debug)
			dev_info(hw->dev, "%s dma[%d]:0x%x\n",
				 __func__, i, (u32)pool->dma[i]);
	}
	return ret;
err:
	rkispp_free_pool(hw);
	return ret;
}

static void rkispp_queue_dmabuf(struct rkispp_hw_dev *hw, struct rkisp_ispp_buf *dbufs)
{
	struct list_head *list = &hw->list;
	struct rkispp_device *ispp;
	struct rkispp_stream_vdev *vdev;
	struct rkisp_ispp_buf *buf = NULL;
	unsigned long lock_flags = 0;
	u32 val;

	spin_lock_irqsave(&hw->buf_lock, lock_flags);
	if (!dbufs)
		hw->is_idle = true;
	if (dbufs && list_empty(list) && hw->is_idle) {
		/* ispp idle or handle same device */
		buf = dbufs;
	} else if (hw->is_idle && !list_empty(list)) {
		/* ispp idle and handle first buf in list */
		buf = list_first_entry(list,
			struct rkisp_ispp_buf, list);
		list_del(&buf->list);
		if (dbufs)
			list_add_tail(&dbufs->list, list);
	} else if (dbufs) {
		/* new buf into queue wait for handle */
		list_add_tail(&dbufs->list, list);
	}

	if (buf) {
		hw->is_idle = false;
		hw->cur_dev_id = buf->index;
		ispp = hw->ispp[buf->index];
		vdev = &ispp->stream_vdev;
		val = (vdev->module_ens & ISPP_MODULE_TNR) ? ISPP_MODULE_TNR : ISPP_MODULE_NR;
		rkispp_module_work_event(ispp, buf, NULL, val, false);
	}

	spin_unlock_irqrestore(&hw->buf_lock, lock_flags);
}

int rkispp_event_handle(struct rkispp_device *ispp, u32 cmd, void *arg)
{
	struct rkispp_hw_dev *hw = ispp->hw_dev;
	int ret = 0;

	switch (cmd) {
	case CMD_STREAM:
		if (*(int *)arg)
			atomic_inc(&hw->refcnt);
		else
			atomic_dec(&hw->refcnt);
		break;
	case CMD_INIT_POOL:
		ret = rkispp_init_pool(hw, arg);
		break;
	case CMD_FREE_POOL:
		rkispp_free_pool(hw);
		break;
	case CMD_QUEUE_DMABUF:
		rkispp_queue_dmabuf(hw, arg);
		break;
	default:
		ret = -EFAULT;
	}

	return ret;
}

void rkispp_soft_reset(struct rkispp_device *ispp)
{
	struct rkispp_hw_dev *hw = ispp->hw_dev;
	struct iommu_domain *domain = iommu_get_domain_for_dev(hw->dev);

	if (domain)
		iommu_detach_device(domain, hw->dev);
	writel(GLB_SOFT_RST_ALL, hw->base_addr + RKISPP_CTRL_RESET);
	udelay(10);
	if (domain)
		iommu_attach_device(domain, hw->dev);
}

int rkispp_alloc_common_dummy_buf(struct rkispp_device *dev)
{
	struct rkispp_hw_dev *hw = dev->hw_dev;
	struct rkispp_subdev *sdev = &dev->ispp_sdev;
	struct rkispp_dummy_buffer *dummy_buf = &hw->dummy_buf;
	u32 w = hw->max_in.w ? hw->max_in.w : sdev->out_fmt.width;
	u32 h =  hw->max_in.h ? hw->max_in.h : sdev->out_fmt.height;
	u32 size =  w * h * 2;
	int ret = 0;

	mutex_lock(&hw->dev_lock);
	if (dummy_buf->mem_priv) {
		if (dummy_buf->size >= size)
			goto end;
		rkispp_free_buffer(dev, &dev->hw_dev->dummy_buf);
	}
	dummy_buf->size = w * h * 2;
	ret = rkispp_allow_buffer(dev, dummy_buf);
	if (ret < 0)
		v4l2_err(&dev->v4l2_dev,
			 "failed to alloc common dummy buf:%d\n", ret);
	else
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "alloc common dummy buf:0x%x size:%d\n",
			 (u32)dummy_buf->dma_addr, dummy_buf->size);

end:
	mutex_unlock(&hw->dev_lock);
	return ret;
}

void rkispp_free_common_dummy_buf(struct rkispp_device *dev)
{
	if (atomic_read(&dev->hw_dev->refcnt))
		return;
	rkispp_free_buffer(dev, &dev->hw_dev->dummy_buf);
}
