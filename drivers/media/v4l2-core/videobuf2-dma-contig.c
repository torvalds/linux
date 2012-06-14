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
#include <linux/scatterlist.h>
#include <linux/sched.h>
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
	unsigned long			size;
	dma_addr_t			dma_addr;
	enum dma_data_direction		dma_dir;
	struct sg_table			*dma_sgt;

	/* MMAP related */
	struct vb2_vmarea_handler	handler;
	atomic_t			refcount;

	/* USERPTR related */
	struct vm_area_struct		*vma;
};

/*********************************************/
/*        scatterlist table functions        */
/*********************************************/


static void vb2_dc_sgt_foreach_page(struct sg_table *sgt,
	void (*cb)(struct page *pg))
{
	struct scatterlist *s;
	unsigned int i;

	for_each_sg(sgt->sgl, s, sgt->orig_nents, i) {
		struct page *page = sg_page(s);
		unsigned int n_pages = PAGE_ALIGN(s->offset + s->length)
			>> PAGE_SHIFT;
		unsigned int j;

		for (j = 0; j < n_pages; ++j, ++page)
			cb(page);
	}
}

static unsigned long vb2_dc_get_contiguous_size(struct sg_table *sgt)
{
	struct scatterlist *s;
	dma_addr_t expected = sg_dma_address(sgt->sgl);
	unsigned int i;
	unsigned long size = 0;

	for_each_sg(sgt->sgl, s, sgt->nents, i) {
		if (sg_dma_address(s) != expected)
			break;
		expected = sg_dma_address(s) + sg_dma_len(s);
		size += sg_dma_len(s);
	}
	return size;
}

/*********************************************/
/*         callbacks for all buffers         */
/*********************************************/

static void *vb2_dc_cookie(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	return &buf->dma_addr;
}

static void *vb2_dc_vaddr(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	return buf->vaddr;
}

static unsigned int vb2_dc_num_users(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	return atomic_read(&buf->refcount);
}

/*********************************************/
/*        callbacks for MMAP buffers         */
/*********************************************/

static void vb2_dc_put(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	if (!atomic_dec_and_test(&buf->refcount))
		return;

	dma_free_coherent(buf->dev, buf->size, buf->vaddr, buf->dma_addr);
	kfree(buf);
}

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

/*********************************************/
/*       callbacks for USERPTR buffers       */
/*********************************************/

static inline int vma_is_io(struct vm_area_struct *vma)
{
	return !!(vma->vm_flags & (VM_IO | VM_PFNMAP));
}

static int vb2_dc_get_user_pages(unsigned long start, struct page **pages,
	int n_pages, struct vm_area_struct *vma, int write)
{
	if (vma_is_io(vma)) {
		unsigned int i;

		for (i = 0; i < n_pages; ++i, start += PAGE_SIZE) {
			unsigned long pfn;
			int ret = follow_pfn(vma, start, &pfn);

			if (ret) {
				pr_err("no page for address %lu\n", start);
				return ret;
			}
			pages[i] = pfn_to_page(pfn);
		}
	} else {
		int n;

		n = get_user_pages(current, current->mm, start & PAGE_MASK,
			n_pages, write, 1, pages, NULL);
		/* negative error means that no page was pinned */
		n = max(n, 0);
		if (n != n_pages) {
			pr_err("got only %d of %d user pages\n", n, n_pages);
			while (n)
				put_page(pages[--n]);
			return -EFAULT;
		}
	}

	return 0;
}

static void vb2_dc_put_dirty_page(struct page *page)
{
	set_page_dirty_lock(page);
	put_page(page);
}

static void vb2_dc_put_userptr(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;
	struct sg_table *sgt = buf->dma_sgt;

	dma_unmap_sg(buf->dev, sgt->sgl, sgt->orig_nents, buf->dma_dir);
	if (!vma_is_io(buf->vma))
		vb2_dc_sgt_foreach_page(sgt, vb2_dc_put_dirty_page);

	sg_free_table(sgt);
	kfree(sgt);
	vb2_put_vma(buf->vma);
	kfree(buf);
}

static void *vb2_dc_get_userptr(void *alloc_ctx, unsigned long vaddr,
	unsigned long size, int write)
{
	struct vb2_dc_conf *conf = alloc_ctx;
	struct vb2_dc_buf *buf;
	unsigned long start;
	unsigned long end;
	unsigned long offset;
	struct page **pages;
	int n_pages;
	int ret = 0;
	struct vm_area_struct *vma;
	struct sg_table *sgt;
	unsigned long contig_size;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dev = conf->dev;
	buf->dma_dir = write ? DMA_FROM_DEVICE : DMA_TO_DEVICE;

	start = vaddr & PAGE_MASK;
	offset = vaddr & ~PAGE_MASK;
	end = PAGE_ALIGN(vaddr + size);
	n_pages = (end - start) >> PAGE_SHIFT;

	pages = kmalloc(n_pages * sizeof(pages[0]), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		pr_err("failed to allocate pages table\n");
		goto fail_buf;
	}

	/* current->mm->mmap_sem is taken by videobuf2 core */
	vma = find_vma(current->mm, vaddr);
	if (!vma) {
		pr_err("no vma for address %lu\n", vaddr);
		ret = -EFAULT;
		goto fail_pages;
	}

	if (vma->vm_end < vaddr + size) {
		pr_err("vma at %lu is too small for %lu bytes\n", vaddr, size);
		ret = -EFAULT;
		goto fail_pages;
	}

	buf->vma = vb2_get_vma(vma);
	if (!buf->vma) {
		pr_err("failed to copy vma\n");
		ret = -ENOMEM;
		goto fail_pages;
	}

	/* extract page list from userspace mapping */
	ret = vb2_dc_get_user_pages(start, pages, n_pages, vma, write);
	if (ret) {
		pr_err("failed to get user pages\n");
		goto fail_vma;
	}

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		pr_err("failed to allocate sg table\n");
		ret = -ENOMEM;
		goto fail_get_user_pages;
	}

	ret = sg_alloc_table_from_pages(sgt, pages, n_pages,
		offset, size, GFP_KERNEL);
	if (ret) {
		pr_err("failed to initialize sg table\n");
		goto fail_sgt;
	}

	/* pages are no longer needed */
	kfree(pages);
	pages = NULL;

	sgt->nents = dma_map_sg(buf->dev, sgt->sgl, sgt->orig_nents,
		buf->dma_dir);
	if (sgt->nents <= 0) {
		pr_err("failed to map scatterlist\n");
		ret = -EIO;
		goto fail_sgt_init;
	}

	contig_size = vb2_dc_get_contiguous_size(sgt);
	if (contig_size < size) {
		pr_err("contiguous mapping is too small %lu/%lu\n",
			contig_size, size);
		ret = -EFAULT;
		goto fail_map_sg;
	}

	buf->dma_addr = sg_dma_address(sgt->sgl);
	buf->size = size;
	buf->dma_sgt = sgt;

	return buf;

fail_map_sg:
	dma_unmap_sg(buf->dev, sgt->sgl, sgt->orig_nents, buf->dma_dir);

fail_sgt_init:
	if (!vma_is_io(buf->vma))
		vb2_dc_sgt_foreach_page(sgt, put_page);
	sg_free_table(sgt);

fail_sgt:
	kfree(sgt);

fail_get_user_pages:
	if (pages && !vma_is_io(buf->vma))
		while (n_pages)
			put_page(pages[--n_pages]);

fail_vma:
	vb2_put_vma(buf->vma);

fail_pages:
	kfree(pages); /* kfree is NULL-proof */

fail_buf:
	kfree(buf);

	return ERR_PTR(ret);
}

/*********************************************/
/*       DMA CONTIG exported functions       */
/*********************************************/

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
