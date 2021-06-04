// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Rockchip Electronics Co., Ltd */

#include <media/videobuf2-dma-contig.h>
#include <linux/delay.h>
#include <linux/of_platform.h>
#include "dev.h"
#include "regs.h"

void rkispp_write(struct rkispp_device *dev, u32 reg, u32 val)
{
	u32 *mem = dev->sw_base_addr + reg;
	u32 *flag = dev->sw_base_addr + reg + RKISP_ISPP_SW_REG_SIZE;

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

	if (end > RKISP_ISPP_SW_REG_SIZE - 4) {
		dev_err(dev->dev, "%s out of range\n", __func__);
		return;
	}
	for (i = start; i <= end; i += 4) {
		u32 *val = dev->sw_base_addr + i;
		u32 *flag = dev->sw_base_addr + i + RKISP_ISPP_SW_REG_SIZE;

		if (*flag == SW_REG_CACHE)
			writel(*val, base + i);
	}
}

int rkispp_allow_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf)
{
	unsigned long attrs = buf->is_need_vaddr ? 0 : DMA_ATTR_NO_KERNEL_MAPPING;
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;
	struct sg_table  *sg_tbl;
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
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;

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

void rkispp_prepare_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;

	if (buf && buf->mem_priv)
		g_ops->prepare(buf->mem_priv);
}

void rkispp_finish_buffer(struct rkispp_device *dev,
			struct rkispp_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;

	if (buf && buf->mem_priv)
		g_ops->finish(buf->mem_priv);
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

static int rkispp_init_regbuf(struct rkispp_hw_dev *hw)
{
	struct rkisp_ispp_reg *reg_buf;
	u32 i, buf_size;

	if (!rkispp_is_reg_withstream_global()) {
		hw->reg_buf = NULL;
		return 0;
	}

	buf_size = RKISP_ISPP_REGBUF_NUM * sizeof(struct rkisp_ispp_reg);
	hw->reg_buf = vmalloc(buf_size);
	if (!hw->reg_buf)
		return -ENOMEM;

	reg_buf = hw->reg_buf;
	for (i = 0; i < RKISP_ISPP_REGBUF_NUM; i++) {
		reg_buf[i].stat = ISP_ISPP_FREE;
		reg_buf[i].dev_id = 0xFF;
		reg_buf[i].frame_id = 0;
		reg_buf[i].reg_size = 0;
		reg_buf[i].sof_timestamp = 0LL;
		reg_buf[i].frame_timestamp = 0LL;
	}

	return 0;
}

static void rkispp_free_regbuf(struct rkispp_hw_dev *hw)
{
	if (hw->reg_buf) {
		vfree(hw->reg_buf);
		hw->reg_buf = NULL;
	}
}

static int rkispp_find_regbuf_by_stat(struct rkispp_hw_dev *hw, struct rkisp_ispp_reg **free_buf,
				      enum rkisp_ispp_reg_stat stat)
{
	struct rkisp_ispp_reg *reg_buf = hw->reg_buf;
	int i = 0, ret;

	*free_buf = NULL;
	if (!hw->reg_buf || !rkispp_reg_withstream)
		return -EINVAL;

	for (i = 0; i < RKISP_ISPP_REGBUF_NUM; i++) {
		if (reg_buf[i].stat == stat)
			break;
	}

	ret = -ENODATA;
	if (i < RKISP_ISPP_REGBUF_NUM) {
		ret = 0;
		*free_buf = &reg_buf[i];
	}

	return ret;
}

static void rkispp_free_pool(struct rkispp_hw_dev *hw)
{
	const struct vb2_mem_ops *g_ops = hw->mem_ops;
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

	rkispp_free_regbuf(hw);
	hw->is_idle = true;
}

static int rkispp_init_pool(struct rkispp_hw_dev *hw, struct rkisp_ispp_buf *dbufs)
{
	const struct vb2_mem_ops *g_ops = hw->mem_ops;
	struct rkispp_isp_buf_pool *pool;
	struct sg_table	 *sg_tbl;
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
		if (hw->is_dma_sg_ops) {
			sg_tbl = (struct sg_table *)g_ops->cookie(mem);
			pool->dma[i] = sg_dma_address(sg_tbl->sgl);
		} else {
			pool->dma[i] = *((dma_addr_t *)g_ops->cookie(mem));
		}
		if (rkispp_debug)
			dev_info(hw->dev, "%s dma[%d]:0x%x\n",
				 __func__, i, (u32)pool->dma[i]);

		pool->vaddr[i] = g_ops->vaddr(mem);
	}
	rkispp_init_regbuf(hw);
	hw->is_idle = true;
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
	if (hw->is_shutdown)
		hw->is_idle = false;
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
		rkispp_params_cfg(&ispp->params_vdev, buf->frame_id);
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

static int rkispp_alloc_page_dummy_buf(struct rkispp_device *dev, u32 size)
{
	struct rkispp_hw_dev *hw = dev->hw_dev;
	struct rkispp_dummy_buffer *dummy_buf = &hw->dummy_buf;
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
	v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
		 "%s buf:0x%x map cnt:%d\n", __func__,
		 (u32)dummy_buf->dma_addr, ret);
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

static void rkispp_free_page_dummy_buf(struct rkispp_device *dev)
{
	struct rkispp_dummy_buffer *dummy_buf = &dev->hw_dev->dummy_buf;
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
	if (dummy_buf->mem_priv)
		goto end;

	if (hw->is_mmu) {
		ret = rkispp_alloc_page_dummy_buf(dev, size);
		goto end;
	}

	dummy_buf->size = size;
	ret = rkispp_allow_buffer(dev, dummy_buf);
	if (!ret)
		v4l2_dbg(1, rkispp_debug, &dev->v4l2_dev,
			 "%s buf:0x%x size:%d\n", __func__,
			 (u32)dummy_buf->dma_addr, dummy_buf->size);
end:
	if (ret < 0)
		v4l2_err(&dev->v4l2_dev, "%s failed:%d\n", __func__, ret);
	mutex_unlock(&hw->dev_lock);
	return ret;
}

void rkispp_free_common_dummy_buf(struct rkispp_device *dev)
{
	struct rkispp_hw_dev *hw = dev->hw_dev;

	mutex_lock(&hw->dev_lock);
	if (atomic_read(&hw->refcnt) ||
	    atomic_read(&dev->stream_vdev.refcnt) > 1)
		goto end;
	if (hw->is_mmu)
		rkispp_free_page_dummy_buf(dev);
	else
		rkispp_free_buffer(dev, &hw->dummy_buf);
end:
	mutex_unlock(&hw->dev_lock);
}

int rkispp_find_regbuf_by_id(struct rkispp_device *ispp, struct rkisp_ispp_reg **free_buf,
			     u32 dev_id, u32 frame_id)
{
	struct rkispp_hw_dev *hw = ispp->hw_dev;
	struct rkisp_ispp_reg *reg_buf = hw->reg_buf;
	int i = 0, ret;

	*free_buf = NULL;
	if (!hw->reg_buf)
		return -EINVAL;

	for (i = 0; i < RKISP_ISPP_REGBUF_NUM; i++) {
		if (reg_buf[i].dev_id == dev_id && reg_buf[i].frame_id == frame_id)
			break;
	}

	ret = -ENODATA;
	if (i < RKISP_ISPP_REGBUF_NUM) {
		ret = 0;
		*free_buf = &reg_buf[i];
	}

	return ret;
}

void rkispp_release_regbuf(struct rkispp_device *ispp, struct rkisp_ispp_reg *freebuf)
{
	struct rkispp_hw_dev *hw = ispp->hw_dev;
	struct rkisp_ispp_reg *reg_buf = hw->reg_buf;
	int i;

	if (!hw->reg_buf)
		return;

	for (i = 0; i < RKISP_ISPP_REGBUF_NUM; i++) {
		if (reg_buf[i].dev_id == freebuf->dev_id &&
			reg_buf[i].frame_timestamp < freebuf->frame_timestamp) {
			reg_buf[i].frame_id = 0;
			reg_buf[i].stat = ISP_ISPP_FREE;
		}
	}
}

void rkispp_request_regbuf(struct rkispp_device *dev, struct rkisp_ispp_reg **free_buf)
{
	struct rkispp_hw_dev *hw = dev->hw_dev;
	int ret;

	if (!hw->reg_buf) {
		*free_buf = NULL;
		return;
	}

	ret = rkispp_find_regbuf_by_stat(hw, free_buf, ISP_ISPP_FREE);
	if (!ret) {
		(*free_buf)->stat = ISP_ISPP_INUSE;
	}
}

bool rkispp_is_reg_withstream_global(void)
{
	return rkispp_reg_withstream;
}

bool rkispp_is_reg_withstream_local(struct device *dev)
{
	const char *node_name = dev_name(dev);

	if (!node_name)
		return false;

	if (!memcmp(rkispp_reg_withstream_video_name, node_name,
		    strlen(node_name)))
		return true;
	else
		return false;
}
