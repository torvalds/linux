/*
 * videobuf2-dma-sg.c - dma scatter/gather memory allocator for videobuf2
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/dma-resv.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-memops.h>
#include <media/videobuf2-dma-sg.h>

static int debug;
module_param(debug, int, 0644);

#define dprintk(level, fmt, arg...)					\
	do {								\
		if (debug >= level)					\
			printk(KERN_DEBUG "vb2-dma-sg: " fmt, ## arg);	\
	} while (0)

struct vb2_dma_sg_buf {
	struct device			*dev;
	void				*vaddr;
	struct page			**pages;
	struct frame_vector		*vec;
	int				offset;
	enum dma_data_direction		dma_dir;
	struct sg_table			sg_table;
	/*
	 * This will point to sg_table when used with the MMAP or USERPTR
	 * memory model, and to the dma_buf sglist when used with the
	 * DMABUF memory model.
	 */
	struct sg_table			*dma_sgt;
	size_t				size;
	unsigned int			num_pages;
	refcount_t			refcount;
	struct vb2_vmarea_handler	handler;

	struct dma_buf_attachment	*db_attach;

	struct vb2_buffer		*vb;
};

static void vb2_dma_sg_put(void *buf_priv);

static int vb2_dma_sg_alloc_compacted(struct vb2_dma_sg_buf *buf,
		gfp_t gfp_flags)
{
	unsigned int last_page = 0;
	unsigned long size = buf->size;

	while (size > 0) {
		struct page *pages;
		int order;
		int i;

		order = get_order(size);
		/* Don't over allocate*/
		if ((PAGE_SIZE << order) > size)
			order--;

		pages = NULL;
		while (!pages) {
			pages = alloc_pages(GFP_KERNEL | __GFP_ZERO |
					__GFP_NOWARN | gfp_flags, order);
			if (pages)
				break;

			if (order == 0) {
				while (last_page--)
					__free_page(buf->pages[last_page]);
				return -ENOMEM;
			}
			order--;
		}

		split_page(pages, order);
		for (i = 0; i < (1 << order); i++)
			buf->pages[last_page++] = &pages[i];

		size -= PAGE_SIZE << order;
	}

	return 0;
}

static void *vb2_dma_sg_alloc(struct vb2_buffer *vb, struct device *dev,
			      unsigned long size)
{
	struct vb2_dma_sg_buf *buf;
	struct sg_table *sgt;
	int ret;
	int num_pages;

	if (WARN_ON(!dev) || WARN_ON(!size))
		return ERR_PTR(-EINVAL);

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = NULL;
	buf->dma_dir = vb->vb2_queue->dma_dir;
	buf->offset = 0;
	buf->size = size;
	/* size is already page aligned */
	buf->num_pages = size >> PAGE_SHIFT;
	buf->dma_sgt = &buf->sg_table;

	/*
	 * NOTE: dma-sg allocates memory using the page allocator directly, so
	 * there is no memory consistency guarantee, hence dma-sg ignores DMA
	 * attributes passed from the upper layer.
	 */
	buf->pages = kvcalloc(buf->num_pages, sizeof(struct page *), GFP_KERNEL);
	if (!buf->pages)
		goto fail_pages_array_alloc;

	ret = vb2_dma_sg_alloc_compacted(buf, vb->vb2_queue->gfp_flags);
	if (ret)
		goto fail_pages_alloc;

	ret = sg_alloc_table_from_pages(buf->dma_sgt, buf->pages,
			buf->num_pages, 0, size, GFP_KERNEL);
	if (ret)
		goto fail_table_alloc;

	/* Prevent the device from being released while the buffer is used */
	buf->dev = get_device(dev);

	sgt = &buf->sg_table;
	/*
	 * No need to sync to the device, this will happen later when the
	 * prepare() memop is called.
	 */
	if (dma_map_sgtable(buf->dev, sgt, buf->dma_dir,
			    DMA_ATTR_SKIP_CPU_SYNC))
		goto fail_map;

	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_dma_sg_put;
	buf->handler.arg = buf;
	buf->vb = vb;

	refcount_set(&buf->refcount, 1);

	dprintk(1, "%s: Allocated buffer of %d pages\n",
		__func__, buf->num_pages);
	return buf;

fail_map:
	put_device(buf->dev);
	sg_free_table(buf->dma_sgt);
fail_table_alloc:
	num_pages = buf->num_pages;
	while (num_pages--)
		__free_page(buf->pages[num_pages]);
fail_pages_alloc:
	kvfree(buf->pages);
fail_pages_array_alloc:
	kfree(buf);
	return ERR_PTR(-ENOMEM);
}

static void vb2_dma_sg_put(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	struct sg_table *sgt = &buf->sg_table;
	int i = buf->num_pages;

	if (refcount_dec_and_test(&buf->refcount)) {
		dprintk(1, "%s: Freeing buffer of %d pages\n", __func__,
			buf->num_pages);
		dma_unmap_sgtable(buf->dev, sgt, buf->dma_dir,
				  DMA_ATTR_SKIP_CPU_SYNC);
		if (buf->vaddr)
			vm_unmap_ram(buf->vaddr, buf->num_pages);
		sg_free_table(buf->dma_sgt);
		while (--i >= 0)
			__free_page(buf->pages[i]);
		kvfree(buf->pages);
		put_device(buf->dev);
		kfree(buf);
	}
}

static void vb2_dma_sg_prepare(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	struct sg_table *sgt = buf->dma_sgt;

	if (buf->vb->skip_cache_sync_on_prepare)
		return;

	dma_sync_sgtable_for_device(buf->dev, sgt, buf->dma_dir);
}

static void vb2_dma_sg_finish(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	struct sg_table *sgt = buf->dma_sgt;

	if (buf->vb->skip_cache_sync_on_finish)
		return;

	dma_sync_sgtable_for_cpu(buf->dev, sgt, buf->dma_dir);
}

static void *vb2_dma_sg_get_userptr(struct vb2_buffer *vb, struct device *dev,
				    unsigned long vaddr, unsigned long size)
{
	struct vb2_dma_sg_buf *buf;
	struct sg_table *sgt;
	struct frame_vector *vec;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = NULL;
	buf->dev = dev;
	buf->dma_dir = vb->vb2_queue->dma_dir;
	buf->offset = vaddr & ~PAGE_MASK;
	buf->size = size;
	buf->dma_sgt = &buf->sg_table;
	buf->vb = vb;
	vec = vb2_create_framevec(vaddr, size,
				  buf->dma_dir == DMA_FROM_DEVICE ||
				  buf->dma_dir == DMA_BIDIRECTIONAL);
	if (IS_ERR(vec))
		goto userptr_fail_pfnvec;
	buf->vec = vec;

	buf->pages = frame_vector_pages(vec);
	if (IS_ERR(buf->pages))
		goto userptr_fail_sgtable;
	buf->num_pages = frame_vector_count(vec);

	if (sg_alloc_table_from_pages(buf->dma_sgt, buf->pages,
			buf->num_pages, buf->offset, size, 0))
		goto userptr_fail_sgtable;

	sgt = &buf->sg_table;
	/*
	 * No need to sync to the device, this will happen later when the
	 * prepare() memop is called.
	 */
	if (dma_map_sgtable(buf->dev, sgt, buf->dma_dir,
			    DMA_ATTR_SKIP_CPU_SYNC))
		goto userptr_fail_map;

	return buf;

userptr_fail_map:
	sg_free_table(&buf->sg_table);
userptr_fail_sgtable:
	vb2_destroy_framevec(vec);
userptr_fail_pfnvec:
	kfree(buf);
	return ERR_PTR(-ENOMEM);
}

/*
 * @put_userptr: inform the allocator that a USERPTR buffer will no longer
 *		 be used
 */
static void vb2_dma_sg_put_userptr(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	struct sg_table *sgt = &buf->sg_table;
	int i = buf->num_pages;

	dprintk(1, "%s: Releasing userspace buffer of %d pages\n",
	       __func__, buf->num_pages);
	dma_unmap_sgtable(buf->dev, sgt, buf->dma_dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (buf->vaddr)
		vm_unmap_ram(buf->vaddr, buf->num_pages);
	sg_free_table(buf->dma_sgt);
	if (buf->dma_dir == DMA_FROM_DEVICE ||
	    buf->dma_dir == DMA_BIDIRECTIONAL)
		while (--i >= 0)
			set_page_dirty_lock(buf->pages[i]);
	vb2_destroy_framevec(buf->vec);
	kfree(buf);
}

static void *vb2_dma_sg_vaddr(struct vb2_buffer *vb, void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	struct iosys_map map;
	int ret;

	BUG_ON(!buf);

	if (!buf->vaddr) {
		if (buf->db_attach) {
			ret = dma_buf_vmap_unlocked(buf->db_attach->dmabuf, &map);
			buf->vaddr = ret ? NULL : map.vaddr;
		} else {
			buf->vaddr = vm_map_ram(buf->pages, buf->num_pages, -1);
		}
	}

	/* add offset in case userptr is not page-aligned */
	return buf->vaddr ? buf->vaddr + buf->offset : NULL;
}

static unsigned int vb2_dma_sg_num_users(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;

	return refcount_read(&buf->refcount);
}

static int vb2_dma_sg_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	int err;

	if (!buf) {
		printk(KERN_ERR "No memory to map\n");
		return -EINVAL;
	}

	err = vm_map_pages(vma, buf->pages, buf->num_pages);
	if (err) {
		printk(KERN_ERR "Remapping memory, error: %d\n", err);
		return err;
	}

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

struct vb2_dma_sg_attachment {
	struct sg_table sgt;
	enum dma_data_direction dma_dir;
};

static int vb2_dma_sg_dmabuf_ops_attach(struct dma_buf *dbuf,
	struct dma_buf_attachment *dbuf_attach)
{
	struct vb2_dma_sg_attachment *attach;
	unsigned int i;
	struct scatterlist *rd, *wr;
	struct sg_table *sgt;
	struct vb2_dma_sg_buf *buf = dbuf->priv;
	int ret;

	attach = kzalloc(sizeof(*attach), GFP_KERNEL);
	if (!attach)
		return -ENOMEM;

	sgt = &attach->sgt;
	/* Copy the buf->base_sgt scatter list to the attachment, as we can't
	 * map the same scatter list to multiple attachments at the same time.
	 */
	ret = sg_alloc_table(sgt, buf->dma_sgt->orig_nents, GFP_KERNEL);
	if (ret) {
		kfree(attach);
		return -ENOMEM;
	}

	rd = buf->dma_sgt->sgl;
	wr = sgt->sgl;
	for (i = 0; i < sgt->orig_nents; ++i) {
		sg_set_page(wr, sg_page(rd), rd->length, rd->offset);
		rd = sg_next(rd);
		wr = sg_next(wr);
	}

	attach->dma_dir = DMA_NONE;
	dbuf_attach->priv = attach;

	return 0;
}

static void vb2_dma_sg_dmabuf_ops_detach(struct dma_buf *dbuf,
	struct dma_buf_attachment *db_attach)
{
	struct vb2_dma_sg_attachment *attach = db_attach->priv;
	struct sg_table *sgt;

	if (!attach)
		return;

	sgt = &attach->sgt;

	/* release the scatterlist cache */
	if (attach->dma_dir != DMA_NONE)
		dma_unmap_sgtable(db_attach->dev, sgt, attach->dma_dir, 0);
	sg_free_table(sgt);
	kfree(attach);
	db_attach->priv = NULL;
}

static struct sg_table *vb2_dma_sg_dmabuf_ops_map(
	struct dma_buf_attachment *db_attach, enum dma_data_direction dma_dir)
{
	struct vb2_dma_sg_attachment *attach = db_attach->priv;
	struct sg_table *sgt;

	sgt = &attach->sgt;
	/* return previously mapped sg table */
	if (attach->dma_dir == dma_dir)
		return sgt;

	/* release any previous cache */
	if (attach->dma_dir != DMA_NONE) {
		dma_unmap_sgtable(db_attach->dev, sgt, attach->dma_dir, 0);
		attach->dma_dir = DMA_NONE;
	}

	/* mapping to the client with new direction */
	if (dma_map_sgtable(db_attach->dev, sgt, dma_dir, 0)) {
		pr_err("failed to map scatterlist\n");
		return ERR_PTR(-EIO);
	}

	attach->dma_dir = dma_dir;

	return sgt;
}

static void vb2_dma_sg_dmabuf_ops_unmap(struct dma_buf_attachment *db_attach,
	struct sg_table *sgt, enum dma_data_direction dma_dir)
{
	/* nothing to be done here */
}

static void vb2_dma_sg_dmabuf_ops_release(struct dma_buf *dbuf)
{
	/* drop reference obtained in vb2_dma_sg_get_dmabuf */
	vb2_dma_sg_put(dbuf->priv);
}

static int
vb2_dma_sg_dmabuf_ops_begin_cpu_access(struct dma_buf *dbuf,
				       enum dma_data_direction direction)
{
	struct vb2_dma_sg_buf *buf = dbuf->priv;
	struct sg_table *sgt = buf->dma_sgt;

	dma_sync_sg_for_cpu(buf->dev, sgt->sgl, sgt->nents, buf->dma_dir);
	return 0;
}

static int
vb2_dma_sg_dmabuf_ops_end_cpu_access(struct dma_buf *dbuf,
				     enum dma_data_direction direction)
{
	struct vb2_dma_sg_buf *buf = dbuf->priv;
	struct sg_table *sgt = buf->dma_sgt;

	dma_sync_sg_for_device(buf->dev, sgt->sgl, sgt->nents, buf->dma_dir);
	return 0;
}

static int vb2_dma_sg_dmabuf_ops_vmap(struct dma_buf *dbuf,
				      struct iosys_map *map)
{
	struct vb2_dma_sg_buf *buf = dbuf->priv;

	iosys_map_set_vaddr(map, buf->vaddr);

	return 0;
}

static int vb2_dma_sg_dmabuf_ops_mmap(struct dma_buf *dbuf,
	struct vm_area_struct *vma)
{
	dma_resv_assert_held(dbuf->resv);

	return vb2_dma_sg_mmap(dbuf->priv, vma);
}

static const struct dma_buf_ops vb2_dma_sg_dmabuf_ops = {
	.attach = vb2_dma_sg_dmabuf_ops_attach,
	.detach = vb2_dma_sg_dmabuf_ops_detach,
	.map_dma_buf = vb2_dma_sg_dmabuf_ops_map,
	.unmap_dma_buf = vb2_dma_sg_dmabuf_ops_unmap,
	.begin_cpu_access = vb2_dma_sg_dmabuf_ops_begin_cpu_access,
	.end_cpu_access = vb2_dma_sg_dmabuf_ops_end_cpu_access,
	.vmap = vb2_dma_sg_dmabuf_ops_vmap,
	.mmap = vb2_dma_sg_dmabuf_ops_mmap,
	.release = vb2_dma_sg_dmabuf_ops_release,
};

static struct dma_buf *vb2_dma_sg_get_dmabuf(struct vb2_buffer *vb,
					     void *buf_priv,
					     unsigned long flags)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	struct dma_buf *dbuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &vb2_dma_sg_dmabuf_ops;
	exp_info.size = buf->size;
	exp_info.flags = flags;
	exp_info.priv = buf;

	if (WARN_ON(!buf->dma_sgt))
		return NULL;

	dbuf = dma_buf_export(&exp_info);
	if (IS_ERR(dbuf))
		return NULL;

	/* dmabuf keeps reference to vb2 buffer */
	refcount_inc(&buf->refcount);

	return dbuf;
}

/*********************************************/
/*       callbacks for DMABUF buffers        */
/*********************************************/

static int vb2_dma_sg_map_dmabuf(void *mem_priv)
{
	struct vb2_dma_sg_buf *buf = mem_priv;
	struct sg_table *sgt;

	if (WARN_ON(!buf->db_attach)) {
		pr_err("trying to pin a non attached buffer\n");
		return -EINVAL;
	}

	if (WARN_ON(buf->dma_sgt)) {
		pr_err("dmabuf buffer is already pinned\n");
		return 0;
	}

	/* get the associated scatterlist for this buffer */
	sgt = dma_buf_map_attachment_unlocked(buf->db_attach, buf->dma_dir);
	if (IS_ERR(sgt)) {
		pr_err("Error getting dmabuf scatterlist\n");
		return -EINVAL;
	}

	buf->dma_sgt = sgt;
	buf->vaddr = NULL;

	return 0;
}

static void vb2_dma_sg_unmap_dmabuf(void *mem_priv)
{
	struct vb2_dma_sg_buf *buf = mem_priv;
	struct sg_table *sgt = buf->dma_sgt;
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(buf->vaddr);

	if (WARN_ON(!buf->db_attach)) {
		pr_err("trying to unpin a not attached buffer\n");
		return;
	}

	if (WARN_ON(!sgt)) {
		pr_err("dmabuf buffer is already unpinned\n");
		return;
	}

	if (buf->vaddr) {
		dma_buf_vunmap_unlocked(buf->db_attach->dmabuf, &map);
		buf->vaddr = NULL;
	}
	dma_buf_unmap_attachment_unlocked(buf->db_attach, sgt, buf->dma_dir);

	buf->dma_sgt = NULL;
}

static void vb2_dma_sg_detach_dmabuf(void *mem_priv)
{
	struct vb2_dma_sg_buf *buf = mem_priv;

	/* if vb2 works correctly you should never detach mapped buffer */
	if (WARN_ON(buf->dma_sgt))
		vb2_dma_sg_unmap_dmabuf(buf);

	/* detach this attachment */
	dma_buf_detach(buf->db_attach->dmabuf, buf->db_attach);
	kfree(buf);
}

static void *vb2_dma_sg_attach_dmabuf(struct vb2_buffer *vb, struct device *dev,
				      struct dma_buf *dbuf, unsigned long size)
{
	struct vb2_dma_sg_buf *buf;
	struct dma_buf_attachment *dba;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	if (dbuf->size < size)
		return ERR_PTR(-EFAULT);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dev = dev;
	/* create attachment for the dmabuf with the user device */
	dba = dma_buf_attach(dbuf, buf->dev);
	if (IS_ERR(dba)) {
		pr_err("failed to attach dmabuf\n");
		kfree(buf);
		return dba;
	}

	buf->dma_dir = vb->vb2_queue->dma_dir;
	buf->size = size;
	buf->db_attach = dba;
	buf->vb = vb;

	return buf;
}

static void *vb2_dma_sg_cookie(struct vb2_buffer *vb, void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;

	return buf->dma_sgt;
}

const struct vb2_mem_ops vb2_dma_sg_memops = {
	.alloc		= vb2_dma_sg_alloc,
	.put		= vb2_dma_sg_put,
	.get_userptr	= vb2_dma_sg_get_userptr,
	.put_userptr	= vb2_dma_sg_put_userptr,
	.prepare	= vb2_dma_sg_prepare,
	.finish		= vb2_dma_sg_finish,
	.vaddr		= vb2_dma_sg_vaddr,
	.mmap		= vb2_dma_sg_mmap,
	.num_users	= vb2_dma_sg_num_users,
	.get_dmabuf	= vb2_dma_sg_get_dmabuf,
	.map_dmabuf	= vb2_dma_sg_map_dmabuf,
	.unmap_dmabuf	= vb2_dma_sg_unmap_dmabuf,
	.attach_dmabuf	= vb2_dma_sg_attach_dmabuf,
	.detach_dmabuf	= vb2_dma_sg_detach_dmabuf,
	.cookie		= vb2_dma_sg_cookie,
};
EXPORT_SYMBOL_GPL(vb2_dma_sg_memops);

MODULE_DESCRIPTION("dma scatter/gather memory handling routines for videobuf2");
MODULE_AUTHOR("Andrzej Pietrasiewicz");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(DMA_BUF);
