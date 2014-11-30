/*
 * videobuf2-vmalloc.c - vmalloc memory allocator for videobuf2
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Pawel Osciak <pawel@osciak.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>
#include <media/videobuf2-memops.h>

struct vb2_vmalloc_buf {
	void				*vaddr;
	struct page			**pages;
	struct vm_area_struct		*vma;
	enum dma_data_direction		dma_dir;
	unsigned long			size;
	unsigned int			n_pages;
	atomic_t			refcount;
	struct vb2_vmarea_handler	handler;
	struct dma_buf			*dbuf;
};

static void vb2_vmalloc_put(void *buf_priv);

static void *vb2_vmalloc_alloc(void *alloc_ctx, unsigned long size,
			       enum dma_data_direction dma_dir, gfp_t gfp_flags)
{
	struct vb2_vmalloc_buf *buf;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL | gfp_flags);
	if (!buf)
		return NULL;

	buf->size = size;
	buf->vaddr = vmalloc_user(buf->size);
	buf->dma_dir = dma_dir;
	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_vmalloc_put;
	buf->handler.arg = buf;

	if (!buf->vaddr) {
		pr_debug("vmalloc of size %ld failed\n", buf->size);
		kfree(buf);
		return NULL;
	}

	atomic_inc(&buf->refcount);
	return buf;
}

static void vb2_vmalloc_put(void *buf_priv)
{
	struct vb2_vmalloc_buf *buf = buf_priv;

	if (atomic_dec_and_test(&buf->refcount)) {
		vfree(buf->vaddr);
		kfree(buf);
	}
}

static void *vb2_vmalloc_get_userptr(void *alloc_ctx, unsigned long vaddr,
				     unsigned long size,
				     enum dma_data_direction dma_dir)
{
	struct vb2_vmalloc_buf *buf;
	unsigned long first, last;
	int n_pages, offset;
	struct vm_area_struct *vma;
	dma_addr_t physp;

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->dma_dir = dma_dir;
	offset = vaddr & ~PAGE_MASK;
	buf->size = size;


	vma = find_vma(current->mm, vaddr);
	if (vma && (vma->vm_flags & VM_PFNMAP) && (vma->vm_pgoff)) {
		if (vb2_get_contig_userptr(vaddr, size, &vma, &physp))
			goto fail_pages_array_alloc;
		buf->vma = vma;
		buf->vaddr = ioremap_nocache(physp, size);
		if (!buf->vaddr)
			goto fail_pages_array_alloc;
	} else {
		first = vaddr >> PAGE_SHIFT;
		last  = (vaddr + size - 1) >> PAGE_SHIFT;
		buf->n_pages = last - first + 1;
		buf->pages = kzalloc(buf->n_pages * sizeof(struct page *),
				     GFP_KERNEL);
		if (!buf->pages)
			goto fail_pages_array_alloc;

		/* current->mm->mmap_sem is taken by videobuf2 core */
		n_pages = get_user_pages(current, current->mm,
					 vaddr & PAGE_MASK, buf->n_pages,
					 dma_dir == DMA_FROM_DEVICE,
					 1, /* force */
					 buf->pages, NULL);
		if (n_pages != buf->n_pages)
			goto fail_get_user_pages;

		buf->vaddr = vm_map_ram(buf->pages, buf->n_pages, -1,
					PAGE_KERNEL);
		if (!buf->vaddr)
			goto fail_get_user_pages;
	}

	buf->vaddr += offset;
	return buf;

fail_get_user_pages:
	pr_debug("get_user_pages requested/got: %d/%d]\n", n_pages,
		 buf->n_pages);
	while (--n_pages >= 0)
		put_page(buf->pages[n_pages]);
	kfree(buf->pages);

fail_pages_array_alloc:
	kfree(buf);

	return NULL;
}

static void vb2_vmalloc_put_userptr(void *buf_priv)
{
	struct vb2_vmalloc_buf *buf = buf_priv;
	unsigned long vaddr = (unsigned long)buf->vaddr & PAGE_MASK;
	unsigned int i;

	if (buf->pages) {
		if (vaddr)
			vm_unmap_ram((void *)vaddr, buf->n_pages);
		for (i = 0; i < buf->n_pages; ++i) {
			if (buf->dma_dir == DMA_FROM_DEVICE)
				set_page_dirty_lock(buf->pages[i]);
			put_page(buf->pages[i]);
		}
		kfree(buf->pages);
	} else {
		vb2_put_vma(buf->vma);
		iounmap(buf->vaddr);
	}
	kfree(buf);
}

static void *vb2_vmalloc_vaddr(void *buf_priv)
{
	struct vb2_vmalloc_buf *buf = buf_priv;

	if (!buf->vaddr) {
		pr_err("Address of an unallocated plane requested "
		       "or cannot map user pointer\n");
		return NULL;
	}

	return buf->vaddr;
}

static unsigned int vb2_vmalloc_num_users(void *buf_priv)
{
	struct vb2_vmalloc_buf *buf = buf_priv;
	return atomic_read(&buf->refcount);
}

static int vb2_vmalloc_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_vmalloc_buf *buf = buf_priv;
	int ret;

	if (!buf) {
		pr_err("No memory to map\n");
		return -EINVAL;
	}

	ret = remap_vmalloc_range(vma, buf->vaddr, 0);
	if (ret) {
		pr_err("Remapping vmalloc memory, error: %d\n", ret);
		return ret;
	}

	/*
	 * Make sure that vm_areas for 2 buffers won't be merged together
	 */
	vma->vm_flags		|= VM_DONTEXPAND;

	/*
	 * Use common vm_area operations to track buffer refcount.
	 */
	vma->vm_private_data	= &buf->handler;
	vma->vm_ops		= &vb2_common_vm_ops;

	vma->vm_ops->open(vma);

	return 0;
}

/*********************************************/
/*         DMABUF ops for exporters          */
/*********************************************/

struct vb2_vmalloc_attachment {
	struct sg_table sgt;
	enum dma_data_direction dma_dir;
};

static int vb2_vmalloc_dmabuf_ops_attach(struct dma_buf *dbuf, struct device *dev,
	struct dma_buf_attachment *dbuf_attach)
{
	struct vb2_vmalloc_attachment *attach;
	struct vb2_vmalloc_buf *buf = dbuf->priv;
	int num_pages = PAGE_ALIGN(buf->size) / PAGE_SIZE;
	struct sg_table *sgt;
	struct scatterlist *sg;
	void *vaddr = buf->vaddr;
	int ret;
	int i;

	attach = kzalloc(sizeof(*attach), GFP_KERNEL);
	if (!attach)
		return -ENOMEM;

	sgt = &attach->sgt;
	ret = sg_alloc_table(sgt, num_pages, GFP_KERNEL);
	if (ret) {
		kfree(attach);
		return ret;
	}
	for_each_sg(sgt->sgl, sg, sgt->nents, i) {
		struct page *page = vmalloc_to_page(vaddr);

		if (!page) {
			sg_free_table(sgt);
			kfree(attach);
			return -ENOMEM;
		}
		sg_set_page(sg, page, PAGE_SIZE, 0);
		vaddr += PAGE_SIZE;
	}

	attach->dma_dir = DMA_NONE;
	dbuf_attach->priv = attach;
	return 0;
}

static void vb2_vmalloc_dmabuf_ops_detach(struct dma_buf *dbuf,
	struct dma_buf_attachment *db_attach)
{
	struct vb2_vmalloc_attachment *attach = db_attach->priv;
	struct sg_table *sgt;

	if (!attach)
		return;

	sgt = &attach->sgt;

	/* release the scatterlist cache */
	if (attach->dma_dir != DMA_NONE)
		dma_unmap_sg(db_attach->dev, sgt->sgl, sgt->orig_nents,
			attach->dma_dir);
	sg_free_table(sgt);
	kfree(attach);
	db_attach->priv = NULL;
}

static struct sg_table *vb2_vmalloc_dmabuf_ops_map(
	struct dma_buf_attachment *db_attach, enum dma_data_direction dma_dir)
{
	struct vb2_vmalloc_attachment *attach = db_attach->priv;
	/* stealing dmabuf mutex to serialize map/unmap operations */
	struct mutex *lock = &db_attach->dmabuf->lock;
	struct sg_table *sgt;
	int ret;

	mutex_lock(lock);

	sgt = &attach->sgt;
	/* return previously mapped sg table */
	if (attach->dma_dir == dma_dir) {
		mutex_unlock(lock);
		return sgt;
	}

	/* release any previous cache */
	if (attach->dma_dir != DMA_NONE) {
		dma_unmap_sg(db_attach->dev, sgt->sgl, sgt->orig_nents,
			attach->dma_dir);
		attach->dma_dir = DMA_NONE;
	}

	/* mapping to the client with new direction */
	ret = dma_map_sg(db_attach->dev, sgt->sgl, sgt->orig_nents, dma_dir);
	if (ret <= 0) {
		pr_err("failed to map scatterlist\n");
		mutex_unlock(lock);
		return ERR_PTR(-EIO);
	}

	attach->dma_dir = dma_dir;

	mutex_unlock(lock);

	return sgt;
}

static void vb2_vmalloc_dmabuf_ops_unmap(struct dma_buf_attachment *db_attach,
	struct sg_table *sgt, enum dma_data_direction dma_dir)
{
	/* nothing to be done here */
}

static void vb2_vmalloc_dmabuf_ops_release(struct dma_buf *dbuf)
{
	/* drop reference obtained in vb2_vmalloc_get_dmabuf */
	vb2_vmalloc_put(dbuf->priv);
}

static void *vb2_vmalloc_dmabuf_ops_kmap(struct dma_buf *dbuf, unsigned long pgnum)
{
	struct vb2_vmalloc_buf *buf = dbuf->priv;

	return buf->vaddr + pgnum * PAGE_SIZE;
}

static void *vb2_vmalloc_dmabuf_ops_vmap(struct dma_buf *dbuf)
{
	struct vb2_vmalloc_buf *buf = dbuf->priv;

	return buf->vaddr;
}

static int vb2_vmalloc_dmabuf_ops_mmap(struct dma_buf *dbuf,
	struct vm_area_struct *vma)
{
	return vb2_vmalloc_mmap(dbuf->priv, vma);
}

static struct dma_buf_ops vb2_vmalloc_dmabuf_ops = {
	.attach = vb2_vmalloc_dmabuf_ops_attach,
	.detach = vb2_vmalloc_dmabuf_ops_detach,
	.map_dma_buf = vb2_vmalloc_dmabuf_ops_map,
	.unmap_dma_buf = vb2_vmalloc_dmabuf_ops_unmap,
	.kmap = vb2_vmalloc_dmabuf_ops_kmap,
	.kmap_atomic = vb2_vmalloc_dmabuf_ops_kmap,
	.vmap = vb2_vmalloc_dmabuf_ops_vmap,
	.mmap = vb2_vmalloc_dmabuf_ops_mmap,
	.release = vb2_vmalloc_dmabuf_ops_release,
};

static struct dma_buf *vb2_vmalloc_get_dmabuf(void *buf_priv, unsigned long flags)
{
	struct vb2_vmalloc_buf *buf = buf_priv;
	struct dma_buf *dbuf;

	if (WARN_ON(!buf->vaddr))
		return NULL;

	dbuf = dma_buf_export(buf, &vb2_vmalloc_dmabuf_ops, buf->size, flags, NULL);
	if (IS_ERR(dbuf))
		return NULL;

	/* dmabuf keeps reference to vb2 buffer */
	atomic_inc(&buf->refcount);

	return dbuf;
}

/*********************************************/
/*       callbacks for DMABUF buffers        */
/*********************************************/

static int vb2_vmalloc_map_dmabuf(void *mem_priv)
{
	struct vb2_vmalloc_buf *buf = mem_priv;

	buf->vaddr = dma_buf_vmap(buf->dbuf);

	return buf->vaddr ? 0 : -EFAULT;
}

static void vb2_vmalloc_unmap_dmabuf(void *mem_priv)
{
	struct vb2_vmalloc_buf *buf = mem_priv;

	dma_buf_vunmap(buf->dbuf, buf->vaddr);
	buf->vaddr = NULL;
}

static void vb2_vmalloc_detach_dmabuf(void *mem_priv)
{
	struct vb2_vmalloc_buf *buf = mem_priv;

	if (buf->vaddr)
		dma_buf_vunmap(buf->dbuf, buf->vaddr);

	kfree(buf);
}

static void *vb2_vmalloc_attach_dmabuf(void *alloc_ctx, struct dma_buf *dbuf,
	unsigned long size, enum dma_data_direction dma_dir)
{
	struct vb2_vmalloc_buf *buf;

	if (dbuf->size < size)
		return ERR_PTR(-EFAULT);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dbuf = dbuf;
	buf->dma_dir = dma_dir;
	buf->size = size;

	return buf;
}


const struct vb2_mem_ops vb2_vmalloc_memops = {
	.alloc		= vb2_vmalloc_alloc,
	.put		= vb2_vmalloc_put,
	.get_userptr	= vb2_vmalloc_get_userptr,
	.put_userptr	= vb2_vmalloc_put_userptr,
	.get_dmabuf	= vb2_vmalloc_get_dmabuf,
	.map_dmabuf	= vb2_vmalloc_map_dmabuf,
	.unmap_dmabuf	= vb2_vmalloc_unmap_dmabuf,
	.attach_dmabuf	= vb2_vmalloc_attach_dmabuf,
	.detach_dmabuf	= vb2_vmalloc_detach_dmabuf,
	.vaddr		= vb2_vmalloc_vaddr,
	.mmap		= vb2_vmalloc_mmap,
	.num_users	= vb2_vmalloc_num_users,
};
EXPORT_SYMBOL_GPL(vb2_vmalloc_memops);

MODULE_DESCRIPTION("vmalloc memory handling routines for videobuf2");
MODULE_AUTHOR("Pawel Osciak <pawel@osciak.com>");
MODULE_LICENSE("GPL");
