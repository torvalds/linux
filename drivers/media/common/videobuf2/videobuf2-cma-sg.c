// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd
 * Based on videobuf2-dma-sg.c
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/refcount.h>
#include <linux/rk-dma-heap.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/dma-map-ops.h>
#include <linux/cma.h>

#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-memops.h>
#include <media/videobuf2-cma-sg.h>

struct vb2_cma_sg_buf {
	struct device			*dev;
	void				*vaddr;
	struct page			**pages;
	struct frame_vector		*vec;
	int				offset;
	unsigned long			dma_attrs;
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
};

static void vb2_cma_sg_put(void *buf_priv);

static int vb2_cma_sg_alloc_compacted(struct vb2_cma_sg_buf *buf,
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

static void vb2_cma_sg_free_compacted(struct vb2_cma_sg_buf *buf)
{
	int num_pages = buf->num_pages;

	while (num_pages--) {
		__free_page(buf->pages[num_pages]);
		buf->pages[num_pages] = NULL;
	}
}

static int vb2_cma_sg_alloc_contiguous(struct vb2_cma_sg_buf *buf)
{
	struct rk_dma_heap *heap __maybe_unused;
	struct page *page = NULL;
	int i;
	bool cma_en = false;

	if (IS_ENABLED(CONFIG_CMA)) {
		struct rk_dma_heap *heap = rk_dma_heap_find("rk-dma-heap-cma");

		cma_en = true;
		if (heap)
			page = rk_dma_heap_alloc_contig_pages(heap, buf->size,
							      dev_name(buf->dev));
		else
			page = cma_alloc(dev_get_cma_area(buf->dev), buf->num_pages,
					 get_order(buf->size), GFP_KERNEL);
	}
	if (IS_ERR_OR_NULL(page)) {
		pr_err("%s: cma_en:%d alloc pages fail\n", __func__, cma_en);
		return -ENOMEM;
	}
	for (i = 0; i < buf->num_pages; i++)
		buf->pages[i] = page + i;

	return 0;
}

static void vb2_cma_sg_free_contiguous(struct vb2_cma_sg_buf *buf)
{
	if (IS_ENABLED(CONFIG_CMA)) {
		struct rk_dma_heap *heap = rk_dma_heap_find("rk-dma-heap-cma");

		if (heap)
			rk_dma_heap_free_contig_pages(heap, buf->pages[0],
						      buf->size, dev_name(buf->dev));
		else
			cma_release(dev_get_cma_area(buf->dev),
				    buf->pages[0], buf->num_pages);
	}
}

static void *vb2_cma_sg_alloc(struct device *dev, unsigned long dma_attrs,
			      unsigned long size,
			      enum dma_data_direction dma_dir,
			      gfp_t gfp_flags)
{
	struct vb2_cma_sg_buf *buf;
	struct sg_table *sgt;
	int ret;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = NULL;
	buf->dma_attrs = dma_attrs;
	buf->dma_dir = dma_dir;
	buf->offset = 0;
	buf->size = size;
	/* size is already page aligned */
	buf->num_pages = size >> PAGE_SHIFT;
	buf->dma_sgt = &buf->sg_table;
	/* Prevent the device from being released while the buffer is used */
	buf->dev = get_device(dev);

	buf->pages = kvmalloc_array(buf->num_pages, sizeof(struct page *),
				    GFP_KERNEL | __GFP_ZERO);
	if (!buf->pages)
		goto fail_pages_array_alloc;

	if (dma_attrs & DMA_ATTR_FORCE_CONTIGUOUS)
		ret = vb2_cma_sg_alloc_contiguous(buf);
	else
		ret = vb2_cma_sg_alloc_compacted(buf, gfp_flags);
	if (ret)
		goto fail_pages_alloc;

	ret = sg_alloc_table_from_pages(buf->dma_sgt, buf->pages,
			buf->num_pages, 0, size, GFP_KERNEL);
	if (ret)
		goto fail_table_alloc;

	sgt = &buf->sg_table;
	/*
	 * No need to sync to the device, this will happen later when the
	 * prepare() memop is called.
	 */
	if (dma_map_sgtable(buf->dev, sgt, buf->dma_dir,
			    DMA_ATTR_SKIP_CPU_SYNC))
		goto fail_map;

	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_cma_sg_put;
	buf->handler.arg = buf;

	refcount_set(&buf->refcount, 1);

	return buf;

fail_map:
	sg_free_table(buf->dma_sgt);
fail_table_alloc:
	if (dma_attrs & DMA_ATTR_FORCE_CONTIGUOUS)
		vb2_cma_sg_free_contiguous(buf);
	else
		vb2_cma_sg_free_compacted(buf);
fail_pages_alloc:
	kvfree(buf->pages);
fail_pages_array_alloc:
	put_device(buf->dev);
	kfree(buf);
	return ERR_PTR(-ENOMEM);
}

static void vb2_cma_sg_put(void *buf_priv)
{
	struct vb2_cma_sg_buf *buf = buf_priv;
	struct sg_table *sgt = &buf->sg_table;

	if (refcount_dec_and_test(&buf->refcount)) {
		dma_unmap_sgtable(buf->dev, sgt, buf->dma_dir,
				  DMA_ATTR_SKIP_CPU_SYNC);
		if (buf->vaddr)
			vm_unmap_ram(buf->vaddr, buf->num_pages);
		sg_free_table(buf->dma_sgt);
		if (buf->dma_attrs & DMA_ATTR_FORCE_CONTIGUOUS)
			vb2_cma_sg_free_contiguous(buf);
		else
			vb2_cma_sg_free_compacted(buf);
		kvfree(buf->pages);
		buf->pages = NULL;
		put_device(buf->dev);
		kfree(buf);
	}
}

static void vb2_cma_sg_prepare(void *buf_priv)
{
	struct vb2_cma_sg_buf *buf = buf_priv;
	struct sg_table *sgt = buf->dma_sgt;

	dma_sync_sgtable_for_device(buf->dev, sgt, buf->dma_dir);
}

static void vb2_cma_sg_finish(void *buf_priv)
{
	struct vb2_cma_sg_buf *buf = buf_priv;
	struct sg_table *sgt = buf->dma_sgt;

	dma_sync_sgtable_for_cpu(buf->dev, sgt, buf->dma_dir);
}

static void *vb2_cma_sg_get_userptr(struct device *dev, unsigned long vaddr,
				    unsigned long size,
				    enum dma_data_direction dma_dir)
{
	struct vb2_cma_sg_buf *buf;
	struct sg_table *sgt;
	struct frame_vector *vec;

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = NULL;
	buf->dev = dev;
	buf->dma_dir = dma_dir;
	buf->offset = vaddr & ~PAGE_MASK;
	buf->size = size;
	buf->dma_sgt = &buf->sg_table;
	vec = vb2_create_framevec(vaddr, size);
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
static void vb2_cma_sg_put_userptr(void *buf_priv)
{
	struct vb2_cma_sg_buf *buf = buf_priv;
	struct sg_table *sgt = &buf->sg_table;
	int i = buf->num_pages;

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

static void *vb2_cma_sg_vaddr(void *buf_priv)
{
	struct vb2_cma_sg_buf *buf = buf_priv;

	WARN_ON(!buf);

	if (!buf->vaddr) {
		if (buf->db_attach)
			buf->vaddr = dma_buf_vmap(buf->db_attach->dmabuf);
		else
			buf->vaddr = vm_map_ram(buf->pages, buf->num_pages, -1);
	}

	/* add offset in case userptr is not page-aligned */
	return buf->vaddr ? buf->vaddr + buf->offset : NULL;
}

static unsigned int vb2_cma_sg_num_users(void *buf_priv)
{
	struct vb2_cma_sg_buf *buf = buf_priv;

	return refcount_read(&buf->refcount);
}

static int vb2_cma_sg_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_cma_sg_buf *buf = buf_priv;
	int err;

	if (!buf) {
		pr_err("No memory to map\n");
		return -EINVAL;
	}

	err = vm_map_pages(vma, buf->pages, buf->num_pages);
	if (err) {
		pr_err("Remapping memory, error: %d\n", err);
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

struct vb2_cma_sg_attachment {
	struct sg_table sgt;
	enum dma_data_direction dma_dir;
};

static int vb2_cma_sg_dmabuf_ops_attach(struct dma_buf *dbuf,
	struct dma_buf_attachment *dbuf_attach)
{
	struct vb2_cma_sg_attachment *attach;
	unsigned int i;
	struct scatterlist *rd, *wr;
	struct sg_table *sgt;
	struct vb2_cma_sg_buf *buf = dbuf->priv;
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

static void vb2_cma_sg_dmabuf_ops_detach(struct dma_buf *dbuf,
	struct dma_buf_attachment *db_attach)
{
	struct vb2_cma_sg_attachment *attach = db_attach->priv;
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

static struct sg_table *vb2_cma_sg_dmabuf_ops_map(
	struct dma_buf_attachment *db_attach, enum dma_data_direction dma_dir)
{
	struct vb2_cma_sg_attachment *attach = db_attach->priv;
	/* stealing dmabuf mutex to serialize map/unmap operations */
	struct mutex *lock = &db_attach->dmabuf->lock;
	struct sg_table *sgt;

	mutex_lock(lock);

	sgt = &attach->sgt;
	/* return previously mapped sg table */
	if (attach->dma_dir == dma_dir) {
		mutex_unlock(lock);
		return sgt;
	}

	/* release any previous cache */
	if (attach->dma_dir != DMA_NONE) {
		dma_unmap_sgtable(db_attach->dev, sgt, attach->dma_dir, 0);
		attach->dma_dir = DMA_NONE;
	}

	/* mapping to the client with new direction */
	if (dma_map_sgtable(db_attach->dev, sgt, dma_dir, 0)) {
		pr_err("failed to map scatterlist\n");
		mutex_unlock(lock);
		return ERR_PTR(-EIO);
	}

	attach->dma_dir = dma_dir;

	mutex_unlock(lock);

	return sgt;
}

static void vb2_cma_sg_dmabuf_ops_unmap(struct dma_buf_attachment *db_attach,
	struct sg_table *sgt, enum dma_data_direction dma_dir)
{
	/* nothing to be done here */
}

static void vb2_cma_sg_dmabuf_ops_release(struct dma_buf *dbuf)
{
	/* drop reference obtained in vb2_cma_sg_get_dmabuf */
	vb2_cma_sg_put(dbuf->priv);
}

static int
vb2_cma_sg_dmabuf_ops_begin_cpu_access(struct dma_buf *dbuf,
				       enum dma_data_direction direction)
{
	struct vb2_cma_sg_buf *buf = dbuf->priv;
	struct sg_table *sgt = buf->dma_sgt;

	dma_sync_sgtable_for_cpu(buf->dev, sgt, buf->dma_dir);
	return 0;
}

static int
vb2_cma_sg_dmabuf_ops_end_cpu_access(struct dma_buf *dbuf,
				     enum dma_data_direction direction)
{
	struct vb2_cma_sg_buf *buf = dbuf->priv;
	struct sg_table *sgt = buf->dma_sgt;

	dma_sync_sgtable_for_device(buf->dev, sgt, buf->dma_dir);
	return 0;
}

static void *vb2_cma_sg_dmabuf_ops_vmap(struct dma_buf *dbuf)
{
	struct vb2_cma_sg_buf *buf = dbuf->priv;

	return vb2_cma_sg_vaddr(buf);
}

static int vb2_cma_sg_dmabuf_ops_mmap(struct dma_buf *dbuf,
	struct vm_area_struct *vma)
{
	return vb2_cma_sg_mmap(dbuf->priv, vma);
}

static const struct dma_buf_ops vb2_cma_sg_dmabuf_ops = {
	.attach = vb2_cma_sg_dmabuf_ops_attach,
	.detach = vb2_cma_sg_dmabuf_ops_detach,
	.map_dma_buf = vb2_cma_sg_dmabuf_ops_map,
	.unmap_dma_buf = vb2_cma_sg_dmabuf_ops_unmap,
	.begin_cpu_access = vb2_cma_sg_dmabuf_ops_begin_cpu_access,
	.end_cpu_access = vb2_cma_sg_dmabuf_ops_end_cpu_access,
	.vmap = vb2_cma_sg_dmabuf_ops_vmap,
	.mmap = vb2_cma_sg_dmabuf_ops_mmap,
	.release = vb2_cma_sg_dmabuf_ops_release,
};

static struct dma_buf *vb2_cma_sg_get_dmabuf(void *buf_priv, unsigned long flags)
{
	struct vb2_cma_sg_buf *buf = buf_priv;
	struct dma_buf *dbuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &vb2_cma_sg_dmabuf_ops;
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

static int vb2_cma_sg_map_dmabuf(void *mem_priv)
{
	struct vb2_cma_sg_buf *buf = mem_priv;
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
	sgt = dma_buf_map_attachment(buf->db_attach, buf->dma_dir);
	if (IS_ERR(sgt)) {
		pr_err("Error getting dmabuf scatterlist\n");
		return -EINVAL;
	}

	buf->dma_sgt = sgt;
	buf->vaddr = NULL;

	return 0;
}

static void vb2_cma_sg_unmap_dmabuf(void *mem_priv)
{
	struct vb2_cma_sg_buf *buf = mem_priv;
	struct sg_table *sgt = buf->dma_sgt;

	if (WARN_ON(!buf->db_attach)) {
		pr_err("trying to unpin a not attached buffer\n");
		return;
	}

	if (WARN_ON(!sgt)) {
		pr_err("dmabuf buffer is already unpinned\n");
		return;
	}

	if (buf->vaddr) {
		dma_buf_vunmap(buf->db_attach->dmabuf, buf->vaddr);
		buf->vaddr = NULL;
	}
	dma_buf_unmap_attachment(buf->db_attach, sgt, buf->dma_dir);

	buf->dma_sgt = NULL;
}

static void vb2_cma_sg_detach_dmabuf(void *mem_priv)
{
	struct vb2_cma_sg_buf *buf = mem_priv;

	/* if vb2 works correctly you should never detach mapped buffer */
	if (WARN_ON(buf->dma_sgt))
		vb2_cma_sg_unmap_dmabuf(buf);

	/* detach this attachment */
	dma_buf_detach(buf->db_attach->dmabuf, buf->db_attach);
	kfree(buf);
}

static void *vb2_cma_sg_attach_dmabuf(struct device *dev, struct dma_buf *dbuf,
	unsigned long size, enum dma_data_direction dma_dir)
{
	struct vb2_cma_sg_buf *buf;
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

	buf->dma_dir = dma_dir;
	buf->size = size;
	buf->db_attach = dba;

	return buf;
}

static void *vb2_cma_sg_cookie(void *buf_priv)
{
	struct vb2_cma_sg_buf *buf = buf_priv;

	return buf->dma_sgt;
}

const struct vb2_mem_ops vb2_cma_sg_memops = {
	.alloc		= vb2_cma_sg_alloc,
	.put		= vb2_cma_sg_put,
	.get_userptr	= vb2_cma_sg_get_userptr,
	.put_userptr	= vb2_cma_sg_put_userptr,
	.prepare	= vb2_cma_sg_prepare,
	.finish		= vb2_cma_sg_finish,
	.vaddr		= vb2_cma_sg_vaddr,
	.mmap		= vb2_cma_sg_mmap,
	.num_users	= vb2_cma_sg_num_users,
	.get_dmabuf	= vb2_cma_sg_get_dmabuf,
	.map_dmabuf	= vb2_cma_sg_map_dmabuf,
	.unmap_dmabuf	= vb2_cma_sg_unmap_dmabuf,
	.attach_dmabuf	= vb2_cma_sg_attach_dmabuf,
	.detach_dmabuf	= vb2_cma_sg_detach_dmabuf,
	.cookie		= vb2_cma_sg_cookie,
};
EXPORT_SYMBOL_GPL(vb2_cma_sg_memops);

MODULE_LICENSE("GPL");
