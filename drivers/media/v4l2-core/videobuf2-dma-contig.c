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

#include <linux/dma-buf.h>
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
	struct sg_table			*sgt_base;

	/* USERPTR related */
	struct vm_area_struct		*vma;

	/* DMABUF related */
	struct dma_buf_attachment	*db_attach;
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

	if (!buf->vaddr && buf->db_attach)
		buf->vaddr = dma_buf_vmap(buf->db_attach->dmabuf);

	return buf->vaddr;
}

static unsigned int vb2_dc_num_users(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	return atomic_read(&buf->refcount);
}

static void vb2_dc_prepare(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;
	struct sg_table *sgt = buf->dma_sgt;

	/* DMABUF exporter will flush the cache for us */
	if (!sgt || buf->db_attach)
		return;

	dma_sync_sg_for_device(buf->dev, sgt->sgl, sgt->nents, buf->dma_dir);
}

static void vb2_dc_finish(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;
	struct sg_table *sgt = buf->dma_sgt;

	/* DMABUF exporter will flush the cache for us */
	if (!sgt || buf->db_attach)
		return;

	dma_sync_sg_for_cpu(buf->dev, sgt->sgl, sgt->nents, buf->dma_dir);
}

/*********************************************/
/*        callbacks for MMAP buffers         */
/*********************************************/

static void vb2_dc_put(void *buf_priv)
{
	struct vb2_dc_buf *buf = buf_priv;

	if (!atomic_dec_and_test(&buf->refcount))
		return;

	if (buf->sgt_base) {
		sg_free_table(buf->sgt_base);
		kfree(buf->sgt_base);
	}
	dma_free_coherent(buf->dev, buf->size, buf->vaddr, buf->dma_addr);
	put_device(buf->dev);
	kfree(buf);
}

static void *vb2_dc_alloc(void *alloc_ctx, unsigned long size,
			  enum dma_data_direction dma_dir, gfp_t gfp_flags)
{
	struct vb2_dc_conf *conf = alloc_ctx;
	struct device *dev = conf->dev;
	struct vb2_dc_buf *buf;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->vaddr = dma_alloc_coherent(dev, size, &buf->dma_addr,
						GFP_KERNEL | gfp_flags);
	if (!buf->vaddr) {
		dev_err(dev, "dma_alloc_coherent of size %ld failed\n", size);
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}

	/* Prevent the device from being released while the buffer is used */
	buf->dev = get_device(dev);
	buf->size = size;
	buf->dma_dir = dma_dir;

	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_dc_put;
	buf->handler.arg = buf;

	atomic_inc(&buf->refcount);

	return buf;
}

static int vb2_dc_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_dc_buf *buf = buf_priv;
	int ret;

	if (!buf) {
		printk(KERN_ERR "No buffer to map\n");
		return -EINVAL;
	}

	/*
	 * dma_mmap_* uses vm_pgoff as in-buffer offset, but we want to
	 * map whole buffer
	 */
	vma->vm_pgoff = 0;

	ret = dma_mmap_coherent(buf->dev, vma, buf->vaddr,
		buf->dma_addr, buf->size);

	if (ret) {
		pr_err("Remapping memory failed, error: %d\n", ret);
		return ret;
	}

	vma->vm_flags		|= VM_DONTEXPAND | VM_DONTDUMP;
	vma->vm_private_data	= &buf->handler;
	vma->vm_ops		= &vb2_common_vm_ops;

	vma->vm_ops->open(vma);

	pr_debug("%s: mapped dma addr 0x%08lx at 0x%08lx, size %ld\n",
		__func__, (unsigned long)buf->dma_addr, vma->vm_start,
		buf->size);

	return 0;
}

/*********************************************/
/*         DMABUF ops for exporters          */
/*********************************************/

struct vb2_dc_attachment {
	struct sg_table sgt;
	enum dma_data_direction dma_dir;
};

static int vb2_dc_dmabuf_ops_attach(struct dma_buf *dbuf, struct device *dev,
	struct dma_buf_attachment *dbuf_attach)
{
	struct vb2_dc_attachment *attach;
	unsigned int i;
	struct scatterlist *rd, *wr;
	struct sg_table *sgt;
	struct vb2_dc_buf *buf = dbuf->priv;
	int ret;

	attach = kzalloc(sizeof(*attach), GFP_KERNEL);
	if (!attach)
		return -ENOMEM;

	sgt = &attach->sgt;
	/* Copy the buf->base_sgt scatter list to the attachment, as we can't
	 * map the same scatter list to multiple attachments at the same time.
	 */
	ret = sg_alloc_table(sgt, buf->sgt_base->orig_nents, GFP_KERNEL);
	if (ret) {
		kfree(attach);
		return -ENOMEM;
	}

	rd = buf->sgt_base->sgl;
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

static void vb2_dc_dmabuf_ops_detach(struct dma_buf *dbuf,
	struct dma_buf_attachment *db_attach)
{
	struct vb2_dc_attachment *attach = db_attach->priv;
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

static struct sg_table *vb2_dc_dmabuf_ops_map(
	struct dma_buf_attachment *db_attach, enum dma_data_direction dma_dir)
{
	struct vb2_dc_attachment *attach = db_attach->priv;
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

static void vb2_dc_dmabuf_ops_unmap(struct dma_buf_attachment *db_attach,
	struct sg_table *sgt, enum dma_data_direction dma_dir)
{
	/* nothing to be done here */
}

static void vb2_dc_dmabuf_ops_release(struct dma_buf *dbuf)
{
	/* drop reference obtained in vb2_dc_get_dmabuf */
	vb2_dc_put(dbuf->priv);
}

static void *vb2_dc_dmabuf_ops_kmap(struct dma_buf *dbuf, unsigned long pgnum)
{
	struct vb2_dc_buf *buf = dbuf->priv;

	return buf->vaddr + pgnum * PAGE_SIZE;
}

static void *vb2_dc_dmabuf_ops_vmap(struct dma_buf *dbuf)
{
	struct vb2_dc_buf *buf = dbuf->priv;

	return buf->vaddr;
}

static int vb2_dc_dmabuf_ops_mmap(struct dma_buf *dbuf,
	struct vm_area_struct *vma)
{
	return vb2_dc_mmap(dbuf->priv, vma);
}

static struct dma_buf_ops vb2_dc_dmabuf_ops = {
	.attach = vb2_dc_dmabuf_ops_attach,
	.detach = vb2_dc_dmabuf_ops_detach,
	.map_dma_buf = vb2_dc_dmabuf_ops_map,
	.unmap_dma_buf = vb2_dc_dmabuf_ops_unmap,
	.kmap = vb2_dc_dmabuf_ops_kmap,
	.kmap_atomic = vb2_dc_dmabuf_ops_kmap,
	.vmap = vb2_dc_dmabuf_ops_vmap,
	.mmap = vb2_dc_dmabuf_ops_mmap,
	.release = vb2_dc_dmabuf_ops_release,
};

static struct sg_table *vb2_dc_get_base_sgt(struct vb2_dc_buf *buf)
{
	int ret;
	struct sg_table *sgt;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		dev_err(buf->dev, "failed to alloc sg table\n");
		return NULL;
	}

	ret = dma_get_sgtable(buf->dev, sgt, buf->vaddr, buf->dma_addr,
		buf->size);
	if (ret < 0) {
		dev_err(buf->dev, "failed to get scatterlist from DMA API\n");
		kfree(sgt);
		return NULL;
	}

	return sgt;
}

static struct dma_buf *vb2_dc_get_dmabuf(void *buf_priv, unsigned long flags)
{
	struct vb2_dc_buf *buf = buf_priv;
	struct dma_buf *dbuf;

	if (!buf->sgt_base)
		buf->sgt_base = vb2_dc_get_base_sgt(buf);

	if (WARN_ON(!buf->sgt_base))
		return NULL;

	dbuf = dma_buf_export(buf, &vb2_dc_dmabuf_ops, buf->size, flags, NULL);
	if (IS_ERR(dbuf))
		return NULL;

	/* dmabuf keeps reference to vb2 buffer */
	atomic_inc(&buf->refcount);

	return dbuf;
}

/*********************************************/
/*       callbacks for USERPTR buffers       */
/*********************************************/

static inline int vma_is_io(struct vm_area_struct *vma)
{
	return !!(vma->vm_flags & (VM_IO | VM_PFNMAP));
}

static int vb2_dc_get_user_pfn(unsigned long start, int n_pages,
	struct vm_area_struct *vma, unsigned long *res)
{
	unsigned long pfn, start_pfn, prev_pfn;
	unsigned int i;
	int ret;

	if (!vma_is_io(vma))
		return -EFAULT;

	ret = follow_pfn(vma, start, &pfn);
	if (ret)
		return ret;

	start_pfn = pfn;
	start += PAGE_SIZE;

	for (i = 1; i < n_pages; ++i, start += PAGE_SIZE) {
		prev_pfn = pfn;
		ret = follow_pfn(vma, start, &pfn);

		if (ret) {
			pr_err("no page for address %lu\n", start);
			return ret;
		}
		if (pfn != prev_pfn + 1)
			return -EINVAL;
	}

	*res = start_pfn;
	return 0;
}

static int vb2_dc_get_user_pages(unsigned long start, struct page **pages,
	int n_pages, struct vm_area_struct *vma,
	enum dma_data_direction dma_dir)
{
	if (vma_is_io(vma)) {
		unsigned int i;

		for (i = 0; i < n_pages; ++i, start += PAGE_SIZE) {
			unsigned long pfn;
			int ret = follow_pfn(vma, start, &pfn);

			if (!pfn_valid(pfn))
				return -EINVAL;

			if (ret) {
				pr_err("no page for address %lu\n", start);
				return ret;
			}
			pages[i] = pfn_to_page(pfn);
		}
	} else {
		int n;

		n = get_user_pages(current, current->mm, start & PAGE_MASK,
			n_pages, dma_dir == DMA_FROM_DEVICE, 1, pages, NULL);
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

	if (sgt) {
		DEFINE_DMA_ATTRS(attrs);

		dma_set_attr(DMA_ATTR_SKIP_CPU_SYNC, &attrs);
		/*
		 * No need to sync to CPU, it's already synced to the CPU
		 * since the finish() memop will have been called before this.
		 */
		dma_unmap_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
				   buf->dma_dir, &attrs);
		if (!vma_is_io(buf->vma))
			vb2_dc_sgt_foreach_page(sgt, vb2_dc_put_dirty_page);

		sg_free_table(sgt);
		kfree(sgt);
	}
	vb2_put_vma(buf->vma);
	kfree(buf);
}

/*
 * For some kind of reserved memory there might be no struct page available,
 * so all that can be done to support such 'pages' is to try to convert
 * pfn to dma address or at the last resort just assume that
 * dma address == physical address (like it has been assumed in earlier version
 * of videobuf2-dma-contig
 */

#ifdef __arch_pfn_to_dma
static inline dma_addr_t vb2_dc_pfn_to_dma(struct device *dev, unsigned long pfn)
{
	return (dma_addr_t)__arch_pfn_to_dma(dev, pfn);
}
#elif defined(__pfn_to_bus)
static inline dma_addr_t vb2_dc_pfn_to_dma(struct device *dev, unsigned long pfn)
{
	return (dma_addr_t)__pfn_to_bus(pfn);
}
#elif defined(__pfn_to_phys)
static inline dma_addr_t vb2_dc_pfn_to_dma(struct device *dev, unsigned long pfn)
{
	return (dma_addr_t)__pfn_to_phys(pfn);
}
#else
static inline dma_addr_t vb2_dc_pfn_to_dma(struct device *dev, unsigned long pfn)
{
	/* really, we cannot do anything better at this point */
	return (dma_addr_t)(pfn) << PAGE_SHIFT;
}
#endif

static void *vb2_dc_get_userptr(void *alloc_ctx, unsigned long vaddr,
	unsigned long size, enum dma_data_direction dma_dir)
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
	unsigned long dma_align = dma_get_cache_alignment();
	DEFINE_DMA_ATTRS(attrs);

	dma_set_attr(DMA_ATTR_SKIP_CPU_SYNC, &attrs);

	/* Only cache aligned DMA transfers are reliable */
	if (!IS_ALIGNED(vaddr | size, dma_align)) {
		pr_debug("user data must be aligned to %lu bytes\n", dma_align);
		return ERR_PTR(-EINVAL);
	}

	if (!size) {
		pr_debug("size is zero\n");
		return ERR_PTR(-EINVAL);
	}

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dev = conf->dev;
	buf->dma_dir = dma_dir;

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
	ret = vb2_dc_get_user_pages(start, pages, n_pages, vma, dma_dir);
	if (ret) {
		unsigned long pfn;
		if (vb2_dc_get_user_pfn(start, n_pages, vma, &pfn) == 0) {
			buf->dma_addr = vb2_dc_pfn_to_dma(buf->dev, pfn);
			buf->size = size;
			kfree(pages);
			return buf;
		}

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

	/*
	 * No need to sync to the device, this will happen later when the
	 * prepare() memop is called.
	 */
	sgt->nents = dma_map_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
				      buf->dma_dir, &attrs);
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
	dma_unmap_sg_attrs(buf->dev, sgt->sgl, sgt->orig_nents,
			   buf->dma_dir, &attrs);

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
/*       callbacks for DMABUF buffers        */
/*********************************************/

static int vb2_dc_map_dmabuf(void *mem_priv)
{
	struct vb2_dc_buf *buf = mem_priv;
	struct sg_table *sgt;
	unsigned long contig_size;

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

	/* checking if dmabuf is big enough to store contiguous chunk */
	contig_size = vb2_dc_get_contiguous_size(sgt);
	if (contig_size < buf->size) {
		pr_err("contiguous chunk is too small %lu/%lu b\n",
			contig_size, buf->size);
		dma_buf_unmap_attachment(buf->db_attach, sgt, buf->dma_dir);
		return -EFAULT;
	}

	buf->dma_addr = sg_dma_address(sgt->sgl);
	buf->dma_sgt = sgt;
	buf->vaddr = NULL;

	return 0;
}

static void vb2_dc_unmap_dmabuf(void *mem_priv)
{
	struct vb2_dc_buf *buf = mem_priv;
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

	buf->dma_addr = 0;
	buf->dma_sgt = NULL;
}

static void vb2_dc_detach_dmabuf(void *mem_priv)
{
	struct vb2_dc_buf *buf = mem_priv;

	/* if vb2 works correctly you should never detach mapped buffer */
	if (WARN_ON(buf->dma_addr))
		vb2_dc_unmap_dmabuf(buf);

	/* detach this attachment */
	dma_buf_detach(buf->db_attach->dmabuf, buf->db_attach);
	kfree(buf);
}

static void *vb2_dc_attach_dmabuf(void *alloc_ctx, struct dma_buf *dbuf,
	unsigned long size, enum dma_data_direction dma_dir)
{
	struct vb2_dc_conf *conf = alloc_ctx;
	struct vb2_dc_buf *buf;
	struct dma_buf_attachment *dba;

	if (dbuf->size < size)
		return ERR_PTR(-EFAULT);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dev = conf->dev;
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

/*********************************************/
/*       DMA CONTIG exported functions       */
/*********************************************/

const struct vb2_mem_ops vb2_dma_contig_memops = {
	.alloc		= vb2_dc_alloc,
	.put		= vb2_dc_put,
	.get_dmabuf	= vb2_dc_get_dmabuf,
	.cookie		= vb2_dc_cookie,
	.vaddr		= vb2_dc_vaddr,
	.mmap		= vb2_dc_mmap,
	.get_userptr	= vb2_dc_get_userptr,
	.put_userptr	= vb2_dc_put_userptr,
	.prepare	= vb2_dc_prepare,
	.finish		= vb2_dc_finish,
	.map_dmabuf	= vb2_dc_map_dmabuf,
	.unmap_dmabuf	= vb2_dc_unmap_dmabuf,
	.attach_dmabuf	= vb2_dc_attach_dmabuf,
	.detach_dmabuf	= vb2_dc_detach_dmabuf,
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
	if (!IS_ERR_OR_NULL(alloc_ctx))
		kfree(alloc_ctx);
}
EXPORT_SYMBOL_GPL(vb2_dma_contig_cleanup_ctx);

MODULE_DESCRIPTION("DMA-contig memory handling routines for videobuf2");
MODULE_AUTHOR("Pawel Osciak <pawel@osciak.com>");
MODULE_LICENSE("GPL");
