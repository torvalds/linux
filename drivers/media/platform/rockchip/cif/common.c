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
		buf->is_free = true;
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

struct rkcif_shm_data {
	void *vaddr;
	int vmap_cnt;
	int npages;
	struct page *pages[];
};

static struct sg_table *rkcif_shm_map_dma_buf(struct dma_buf_attachment *attachment,
					enum dma_data_direction dir)
{
	struct rkcif_shm_data *data = attachment->dmabuf->priv;
	struct sg_table *table;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	sg_alloc_table_from_pages(table, data->pages, data->npages, 0,
				  data->npages << PAGE_SHIFT, GFP_KERNEL);

	dma_map_sgtable(attachment->dev, table, dir, DMA_ATTR_SKIP_CPU_SYNC);

	return table;
}

static void rkcif_shm_unmap_dma_buf(struct dma_buf_attachment *attachment,
			      struct sg_table *table,
			      enum dma_data_direction dir)
{
	dma_unmap_sgtable(attachment->dev, table, dir, DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(table);
	kfree(table);
}

static void *rkcif_shm_vmap(struct dma_buf *dma_buf)
{
	struct rkcif_shm_data *data = dma_buf->priv;

	data->vaddr = vmap(data->pages, data->npages, VM_MAP, PAGE_KERNEL);
	data->vmap_cnt++;
	return data->vaddr;
}

static void rkcif_shm_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
	struct rkcif_shm_data *data = dma_buf->priv;

	vunmap(data->vaddr);
	data->vaddr = NULL;
	data->vmap_cnt--;
}

static int rkcif_shm_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	struct rkcif_shm_data *data = dma_buf->priv;
	unsigned long vm_start = vma->vm_start;
	int i;

	for (i = 0; i < data->npages; i++) {
		remap_pfn_range(vma, vm_start, page_to_pfn(data->pages[i]),
				PAGE_SIZE, vma->vm_page_prot);
		vm_start += PAGE_SIZE;
	}

	return 0;
}

static int rkcif_shm_begin_cpu_access(struct dma_buf *dmabuf, enum dma_data_direction dir)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *table;

	attachment = list_first_entry(&dmabuf->attachments, struct dma_buf_attachment, node);
	table = attachment->priv;
	dma_sync_sg_for_cpu(NULL, table->sgl, table->nents, dir);

	return 0;
}

static int rkcif_shm_end_cpu_access(struct dma_buf *dmabuf, enum dma_data_direction dir)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *table;

	attachment = list_first_entry(&dmabuf->attachments, struct dma_buf_attachment, node);
	table = attachment->priv;
	dma_sync_sg_for_device(NULL, table->sgl, table->nents, dir);

	return 0;
}

static void rkcif_shm_release(struct dma_buf *dma_buf)
{
	struct rkcif_shm_data *data = dma_buf->priv;

	if (data->vmap_cnt) {
		WARN(1, "%s: buffer still mapped in the kernel\n", __func__);
		rkcif_shm_vunmap(dma_buf, data->vaddr);
	}
	kfree(data);
}

static const struct dma_buf_ops rkcif_shm_dmabuf_ops = {
	.map_dma_buf = rkcif_shm_map_dma_buf,
	.unmap_dma_buf = rkcif_shm_unmap_dma_buf,
	.release = rkcif_shm_release,
	.mmap = rkcif_shm_mmap,
	.vmap = rkcif_shm_vmap,
	.vunmap = rkcif_shm_vunmap,
	.begin_cpu_access = rkcif_shm_begin_cpu_access,
	.end_cpu_access = rkcif_shm_end_cpu_access,
};

static struct dma_buf *rkcif_shm_alloc(struct rkisp_thunderboot_shmem *shmem)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	struct rkcif_shm_data *data;
	int i, npages;

	npages = PAGE_ALIGN(shmem->shm_size) / PAGE_SIZE;
	data = kmalloc(sizeof(*data) + npages * sizeof(struct page *), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);
	data->vmap_cnt = 0;
	data->npages = npages;
	for (i = 0; i < npages; i++)
		data->pages[i] = phys_to_page(shmem->shm_start + i * PAGE_SIZE);

	exp_info.ops = &rkcif_shm_dmabuf_ops;
	exp_info.size = npages * PAGE_SIZE;
	exp_info.flags = O_RDWR;
	exp_info.priv = data;

	dmabuf = dma_buf_export(&exp_info);

	return dmabuf;
}

int rkcif_alloc_reserved_mem_buf(struct rkcif_device *dev, struct rkcif_rx_buffer *buf)
{
	struct rkcif_dummy_buffer *dummy = &buf->dummy;

	dummy->dma_addr = dev->resmem_pa + dummy->size * buf->buf_idx;
	if (dummy->dma_addr + dummy->size > dev->resmem_pa + dev->resmem_size)
		return -EINVAL;
	buf->dbufs.dma = dummy->dma_addr;
	buf->dbufs.is_resmem = true;
	buf->shmem.shm_start = dummy->dma_addr;
	buf->shmem.shm_size = dummy->size;
	dummy->dbuf = rkcif_shm_alloc(&buf->shmem);
	if (dummy->is_need_vaddr)
		dummy->vaddr = dummy->dbuf->ops->vmap(dummy->dbuf);
	return 0;
}

void rkcif_free_reserved_mem_buf(struct rkcif_device *dev, struct rkcif_rx_buffer *buf)
{
	struct rkcif_dummy_buffer *dummy = &buf->dummy;
	struct media_pad *pad = NULL;
	struct v4l2_subdev *sd;

	if (buf->dummy.is_free)
		return;

	if (dev->rdbk_debug)
		v4l2_info(&dev->v4l2_dev,
			  "free reserved mem addr 0x%x\n",
			  (u32)dummy->dma_addr);
	if (dev->sditf[0]) {
		if (dev->sditf[0]->is_combine_mode)
			pad = media_entity_remote_pad(&dev->sditf[0]->pads[1]);
		else
			pad = media_entity_remote_pad(&dev->sditf[0]->pads[0]);
	} else {
		v4l2_info(&dev->v4l2_dev,
			  "not find sditf\n");
		return;
	}
	if (pad) {
		sd = media_entity_to_v4l2_subdev(pad->entity);
	} else {
		v4l2_info(&dev->v4l2_dev,
			  "not find remote pad\n");
		return;
	}
	if (buf->dbufs.is_init)
		v4l2_subdev_call(sd, core, ioctl,
				 RKISP_VICAP_CMD_RX_BUFFER_FREE, &buf->dbufs);
	if (dummy->is_need_vaddr)
		dummy->dbuf->ops->vunmap(dummy->dbuf, dummy->vaddr);
#ifdef CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP
	free_reserved_area(phys_to_virt(buf->shmem.shm_start),
			   phys_to_virt(buf->shmem.shm_start + buf->shmem.shm_size),
			   -1, "rkisp_thunderboot");
#endif
	buf->dummy.is_free = true;
}

