/*
 * videobuf2-dma-contig.c - DMA contig memory allocator for videobuf2
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Pawel Osciak <pawel@osciak.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-memops.h>

struct vb2_dc_conf {
	struct device		*dev;
};

struct vb2_dc_buf {
	struct device			*dev;
	void				*vaddr;
	dma_addr_t			dma_addr;
	unsigned long			size;
	struct vm_area_struct		*vma;
	atomic_t			refcount;
	struct vb2_vmarea_handler	handler;
};

static void vb2_dc_put(void *buf_priv);

static void *vb2_dc_alloc(void *alloc_ctx, unsigned long size)
{
	struct vb2_dc_conf *conf = alloc_ctx;
	struct device *dev = conf->dev;
	struct vb2_dc_buf *buf;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = dma_alloc_coherent(dev, size, &buf->dma_addr, GFP_KERNEL);
	if (!buf->vaddr) {
		dev_err(dev, "dma_alloc_coherent of size %ld failed\n", size);
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}

	buf->dev = dev;
	buf->size = size;

	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_dc_put;
	buf->handler.arg = buf;

	atomic_inc(&buf->refcount);

	return buf;
}

static void vb2_dc_put(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	if (atomic_dec_and_test(&buf->refcount)) {
		dma_free_coherent(buf->dev, buf->size, buf->vaddr,
				  buf->dma_addr);
		kfree(buf);
	}
}

static void *vb2_dc_cookie(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	return &buf->dma_addr;
}

static void *vb2_dc_vaddr(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;
	if (!buf)
		return NULL;

	return buf->vaddr;
}

static unsigned int vb2_dc_num_users(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	return atomic_read(&buf->refcount);
}

static int vb2_dc_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_dc_buf *buf = buf_priv;

	if (!buf) {
		printk(KERN_ERR "No buffer to map\n");
		return -EINVAL;
	}

	return vb2_mmap_pfn_range(vma, buf->dma_addr, buf->size,
				  &vb2_common_vm_ops, &buf->handler);
}

static void *vb2_dc_get_userptr(void *alloc_ctx, unsigned long vaddr,
					unsigned long size, int write)
{
	struct vb2_dc_buf *buf;
	struct vm_area_struct *vma;
	dma_addr_t dma_addr = 0;
	int ret;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	ret = vb2_get_contig_userptr(vaddr, size, &vma, &dma_addr);
	if (ret) {
		printk(KERN_ERR "Failed acquiring VMA for vaddr 0x%08lx\n",
				vaddr);
		kfree(buf);
		return ERR_PTR(ret);
	}

	buf->size = size;
	buf->dma_addr = dma_addr;
	buf->vma = vma;

	return buf;
}

static void vb2_dc_put_userptr(void *mem_priv)
{
	struct vb2_dc_buf *buf = mem_priv;

	if (!buf)
		return;

	vb2_put_vma(buf->vma);
	kfree(buf);
}

const struct vb2_mem_ops vb2_dma_contig_memops = {
	.alloc		= vb2_dc_alloc,
	.put		= vb2_dc_put,
	.cookie		= vb2_dc_cookie,
	.vaddr		= vb2_dc_vaddr,
	.mmap		= vb2_dc_mmap,
	.get_userptr	= vb2_dc_get_userptr,
	.put_userptr	= vb2_dc_put_userptr,
	.num_users	= vb2_dc_num_users,
};
EXPORT_SYMBOL_GPL(vb2_dma_contig_memops);

void *vb2_dma_contig_init_ctx(struct device *dev)
{
	struct vb2_dc_conf *conf;

	conf = kzalloc(sizeof *conf, GFP_KERNEL);
	if (!conf)
		return ERR_PTR(-ENOMEM);

	conf->dev = dev;

	return conf;
}
EXPORT_SYMBOL_GPL(vb2_dma_contig_init_ctx);

void vb2_dma_contig_cleanup_ctx(void *alloc_ctx)
{
	kfree(alloc_ctx);
}
EXPORT_SYMBOL_GPL(vb2_dma_contig_cleanup_ctx);

MODULE_DESCRIPTION("DMA-contig memory handling routines for videobuf2");
MODULE_AUTHOR("Pawel Osciak <pawel@osciak.com>");
MODULE_LICENSE("GPL");
