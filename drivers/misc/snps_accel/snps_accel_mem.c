// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <uapi/misc/snps_accel.h>
#include "snps_accel_drv.h"

static struct snps_accel_mem_buffer *
snps_accel_mbuf_alloc(struct snps_accel_mem_ctx *mem, size_t size)
{
	struct page *page;
	struct snps_accel_mem_buffer *mbuf = NULL;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);

	mbuf = kzalloc(sizeof(*mbuf), GFP_KERNEL);
	if (!mbuf)
		return NULL;

	/* Allocate buffer in direct memory */
	page = dma_alloc_pages(mem->dev, PAGE_ALIGN(size), &mbuf->da,
			       DMA_BIDIRECTIONAL, GFP_KERNEL | __GFP_NOWARN);
	if (!page) {
		dev_err(mem->dev, "Failed to allocate contiguous memory for buffer\n");
		return NULL;
	}
	mbuf->ctx = mem;
	mbuf->dev = mem->dev;
	mbuf->va = page_address(page);
	mbuf->pa =  page_to_pfn(page) << PAGE_SHIFT;
	mbuf->size = PAGE_ALIGN(size);

	mutex_init(&mbuf->lock);
	INIT_LIST_HEAD(&mbuf->attachments);

	mutex_lock(&mem->list_lock);
	list_add(&mbuf->ctx_link, &mem->mlist);
	mutex_unlock(&mem->list_lock);

	snps_accel_file_priv_get(fpriv);
	return mbuf;
}

static void
snps_accel_mbuf_free(struct snps_accel_mem_ctx *mem, struct snps_accel_mem_buffer *mbuf)
{
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);

	mutex_lock(&mem->list_lock);
	list_del(&mbuf->ctx_link);
	mutex_unlock(&mem->list_lock);

	dma_free_pages(mbuf->dev, mbuf->size,
		       virt_to_page(mbuf->va),
		       mbuf->da, DMA_BIDIRECTIONAL);

	kfree(mbuf);
	snps_accel_file_priv_put(fpriv);
}

static struct snps_accel_mem_buffer *
snps_accel_dmabuf_find_by_fd(struct snps_accel_mem_ctx *mem, int fd)
{
	struct snps_accel_mem_buffer *mbuf = NULL;

	mutex_lock(&mem->list_lock);
	list_for_each_entry(mbuf, &mem->mlist, ctx_link) {
		if (mbuf->fd == fd) {
			mutex_unlock(&mem->list_lock);
			return mbuf;
		}
	}
	mutex_unlock(&mem->list_lock);

	return NULL;
}

static bool snps_accel_dmabuf_is_contig(struct sg_table *sgt)
{
	struct scatterlist *s;
	dma_addr_t expected = sg_dma_address(sgt->sgl);
	unsigned int i;

	for_each_sgtable_dma_sg(sgt, s, i) {
		if (sg_dma_address(s) != expected)
			return 0;
		expected += sg_dma_len(s);
	}
	return 1;
}

static void snps_accel_dmabuf_op_release(struct dma_buf *dmabuf)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_mem_ctx *mem = mbuf->ctx;

	snps_accel_mbuf_free(mem, mbuf);
}

static int
snps_accel_dmabuf_op_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	size_t size = vma->vm_end - vma->vm_start;
	int ret = 0;

	if (PAGE_ALIGN(size) != mbuf->size)
		return -EINVAL;

	ret = dma_mmap_pages(mbuf->dev, vma, mbuf->size, virt_to_page(mbuf->va));
	if (ret)
		return ret;

	return 0;
}

static int snps_accel_dmabuf_op_attach(struct dma_buf *dmabuf,
				       struct dma_buf_attachment *attachment)
{
	struct snps_accel_dmabuf_attachment *dba;
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mbuf->ctx);
	int ret;

	dba = kzalloc(sizeof(*dba), GFP_KERNEL);
	if (!dba)
		return -ENOMEM;

	ret = dma_get_sgtable(mbuf->dev, &dba->sgt, mbuf->va,
			      mbuf->pa, mbuf->size);
	if (ret < 0) {
		dev_err(mbuf->dev, "Failed to get scatter list from DMA API\n");
		kfree(dba);
		return -EINVAL;
	}

	dba->dev = attachment->dev;
	INIT_LIST_HEAD(&dba->node);
	attachment->priv = dba;
	dba->mapped = false;

	mutex_lock(&mbuf->lock);
	list_add(&dba->node, &mbuf->attachments);
	mutex_unlock(&mbuf->lock);
	snps_accel_file_priv_get(fpriv);

	return 0;
}

static void snps_accel_dmabuf_op_detach(struct dma_buf *dmabuf,
					struct dma_buf_attachment *attachment)
{
	struct snps_accel_dmabuf_attachment *dba = attachment->priv;
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mbuf->ctx);

	mutex_lock(&mbuf->lock);
	list_del(&dba->node);
	mutex_unlock(&mbuf->lock);
	sg_free_table(&dba->sgt);
	kfree(dba);
	snps_accel_file_priv_put(fpriv);
}

static struct sg_table *
snps_accel_dmabuf_op_map(struct dma_buf_attachment *attachment,
			 enum dma_data_direction dir)
{
	struct snps_accel_dmabuf_attachment *dba = attachment->priv;
	struct sg_table *table;
	int ret;

	table = &dba->sgt;
	dba->mapped = true;

	ret = dma_map_sgtable(attachment->dev, table, dir, 0);
	if (ret)
		table = ERR_PTR(ret);

	return table;
}

static void snps_accel_dmabuf_op_unmap(struct dma_buf_attachment *attach,
				  struct sg_table *table,
				  enum dma_data_direction dir)
{
	struct snps_accel_dmabuf_attachment *dba = attach->priv;

	dba->mapped = false;
	dma_unmap_sgtable(attach->dev, table, dir, 0);
}

static int snps_accel_dmabuf_op_begin_cpu_access(struct dma_buf *dmabuf,
						 enum dma_data_direction direction)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_dmabuf_attachment *dba;

	mutex_lock(&mbuf->lock);

	list_for_each_entry(dba, &mbuf->attachments, node) {
		if (!dba->mapped)
			continue;
		dma_sync_sgtable_for_cpu(dba->dev, &dba->sgt, direction);
	}
	mutex_unlock(&mbuf->lock);

	return 0;
}

static int snps_accel_dmabuf_op_end_cpu_access(struct dma_buf *dmabuf,
					       enum dma_data_direction direction)
{
	struct snps_accel_mem_buffer *mbuf = dmabuf->priv;
	struct snps_accel_dmabuf_attachment *dba;

	mutex_lock(&mbuf->lock);

	list_for_each_entry(dba, &mbuf->attachments, node) {
		if (!dba->mapped)
			continue;
		dma_sync_sgtable_for_device(dba->dev, &dba->sgt, direction);
	}
	mutex_unlock(&mbuf->lock);

	return 0;
}

static const struct dma_buf_ops snps_accel_dmabuf_ops = {
	.attach = snps_accel_dmabuf_op_attach,
	.detach = snps_accel_dmabuf_op_detach,
	.map_dma_buf = snps_accel_dmabuf_op_map,
	.unmap_dma_buf = snps_accel_dmabuf_op_unmap,
	.begin_cpu_access = snps_accel_dmabuf_op_begin_cpu_access,
	.end_cpu_access = snps_accel_dmabuf_op_end_cpu_access,
	.mmap = snps_accel_dmabuf_op_mmap,
	.release = snps_accel_dmabuf_op_release,
};

void snps_accel_app_mem_init(struct device *dev, struct snps_accel_mem_ctx *mem)
{
	mem->dev = dev;
	mutex_init(&mem->list_lock);
	INIT_LIST_HEAD(&mem->mlist);
}

static void
snsp_accel_dmabuf_detach_import(struct snps_accel_mem_buffer *mbuf)
{
	if (mbuf->dmasgt)
		dma_buf_unmap_attachment(mbuf->import_attach, mbuf->dmasgt,
					 DMA_BIDIRECTIONAL);
	dma_buf_detach(mbuf->dmabuf, mbuf->import_attach);
	dma_buf_put(mbuf->import_attach->dmabuf);
}

void snps_accel_app_release_import(struct snps_accel_mem_ctx *mem)
{
	struct snps_accel_mem_buffer *mbuf, *nmb;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);

	mutex_lock(&mem->list_lock);
	list_for_each_entry_safe(mbuf, nmb, &mem->mlist, ctx_link) {
		if (mbuf->import_attach) {
			snsp_accel_dmabuf_detach_import(mbuf);
			list_del(&mbuf->ctx_link);
			kfree(mbuf);
			snps_accel_file_priv_put(fpriv);
		}
	}
	mutex_unlock(&mem->list_lock);
}

struct snps_accel_mem_buffer *snps_accel_app_dmabuf_create(struct snps_accel_mem_ctx *mem,
							   u64 size, u32 dflags)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct snps_accel_mem_buffer *mbuf = NULL;
	int fd;

	mbuf = snps_accel_mbuf_alloc(mem, size);
	if (mbuf == NULL)
		return NULL;

	exp_info.ops = &snps_accel_dmabuf_ops;
	exp_info.size = size;
	exp_info.flags = O_RDWR;
	exp_info.priv = mbuf;
	mbuf->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(mbuf->dmabuf)) {
		snps_accel_mbuf_free(mem, mbuf);
		return NULL;
	}

	fd = dma_buf_fd(mbuf->dmabuf, O_ACCMODE | O_CLOEXEC);
	if (fd < 0) {
		dma_buf_put(mbuf->dmabuf);
		return NULL;
	}
	mbuf->fd = fd;

	return mbuf;
}

int snps_accel_app_dmabuf_info(struct snps_accel_dmabuf_info *info)
{
	struct dma_buf *dmabuf;
	struct snps_accel_mem_buffer *mbuf;

	dmabuf = dma_buf_get(info->fd);
	if (!dmabuf)
		return -EINVAL;

	mbuf = (struct snps_accel_mem_buffer *)dmabuf->priv;
	info->addr = mbuf->da;
	info->size = mbuf->size;

	dma_buf_put(dmabuf);
	return 0;
}

void snps_accel_app_dmabuf_release(struct snps_accel_mem_buffer *mbuf)
{
	dma_buf_put(mbuf->dmabuf);
}

int snps_accel_app_dmabuf_import(struct snps_accel_mem_ctx *mem, int fd)
{
	struct dma_buf *dmabuf;
	struct snps_accel_mem_buffer *mbuf;
	struct dma_buf_attachment *dba;
	struct sg_table *sgt;
	int ret;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);

	dmabuf = dma_buf_get(fd);
	if (IS_ERR_OR_NULL(dmabuf)) {
		dev_err(mem->dev, "Failed to get dma_buf with fd %d\n", fd);
		return -EINVAL;
	}

	mbuf = kzalloc(sizeof(*mbuf), GFP_KERNEL);
	if (!mbuf) {
		dma_buf_put(dmabuf);
		ret = -ENOMEM;
		goto err_alloc;
	}

	mbuf->dev = mem->dev;
	mbuf->fd = fd;

	dba = dma_buf_attach(dmabuf, mbuf->dev);
	if (IS_ERR(dba)) {
		dev_err(mem->dev, "Failed to attach dmabuf\n");
		ret = PTR_ERR(dba);
		goto err_attach;
	}

	/* Get the associated scatter list for this buffer */
	sgt = dma_buf_map_attachment(dba, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		dev_err(mem->dev, "Failed to get dmabuf scatter list\n");
		ret = -EINVAL;
		goto err_map;
	}
	if (!snps_accel_dmabuf_is_contig(sgt)) {
		ret = -EINVAL;
		goto err_notcontig;
	}

	mbuf->size = dba->dmabuf->size;
	mbuf->dmabuf = dba->dmabuf;
	mbuf->da = sg_dma_address(sgt->sgl);
	mbuf->dmasgt = sgt;
	mbuf->va = NULL;
	mbuf->import_attach = dba;

	mutex_lock(&mem->list_lock);
	list_add(&mbuf->ctx_link, &mem->mlist);
	mutex_unlock(&mem->list_lock);

	snps_accel_file_priv_get(fpriv);

	return 0;

err_notcontig:
	dma_buf_unmap_attachment(dba, sgt, DMA_BIDIRECTIONAL);
err_map:
	dma_buf_detach(dmabuf, dba);
err_attach:
	kfree(mbuf);
err_alloc:
	dma_buf_put(dmabuf);
	return ret;
}

int snps_accel_app_dmabuf_detach(struct snps_accel_mem_ctx *mem, int fd)
{
	struct snps_accel_mem_buffer *mbuf;
	struct snps_accel_file_priv *fpriv = to_snps_accel_file_priv(mem);

	mbuf = snps_accel_dmabuf_find_by_fd(mem, fd);
	if (!mbuf) {
		dev_err(mem->dev, "Failed to find imported dmabuf with fd %d\n", fd);
		return -EINVAL;
	}
	snsp_accel_dmabuf_detach_import(mbuf);

	mutex_lock(&mem->list_lock);
	list_del(&mbuf->ctx_link);
	mutex_unlock(&mem->list_lock);

	kfree(mbuf);
	snps_accel_file_priv_put(fpriv);

	return 0;
}
