// SPDX-License-Identifier: GPL-2.0

/*
 * Xen dma-buf functionality for gntdev.
 *
 * DMA buffer implementation is based on drivers/gpu/drm/drm_prime.c.
 *
 * Copyright (c) 2018 Oleksandr Andrushchenko, EPAM Systems Inc.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/dma-buf.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <xen/xen.h>
#include <xen/grant_table.h>

#include "gntdev-common.h"
#include "gntdev-dmabuf.h"

#ifndef GRANT_INVALID_REF
/*
 * Note on usage of grant reference 0 as invalid grant reference:
 * grant reference 0 is valid, but never exposed to a driver,
 * because of the fact it is already in use/reserved by the PV console.
 */
#define GRANT_INVALID_REF	0
#endif

struct gntdev_dmabuf {
	struct gntdev_dmabuf_priv *priv;
	struct dma_buf *dmabuf;
	struct list_head next;
	int fd;

	union {
		struct {
			/* Exported buffers are reference counted. */
			struct kref refcount;

			struct gntdev_priv *priv;
			struct gntdev_grant_map *map;
		} exp;
		struct {
			/* Granted references of the imported buffer. */
			grant_ref_t *refs;
			/* Scatter-gather table of the imported buffer. */
			struct sg_table *sgt;
			/* dma-buf attachment of the imported buffer. */
			struct dma_buf_attachment *attach;
		} imp;
	} u;

	/* Number of pages this buffer has. */
	int nr_pages;
	/* Pages of this buffer. */
	struct page **pages;
};

struct gntdev_dmabuf_wait_obj {
	struct list_head next;
	struct gntdev_dmabuf *gntdev_dmabuf;
	struct completion completion;
};

struct gntdev_dmabuf_attachment {
	struct sg_table *sgt;
	enum dma_data_direction dir;
};

struct gntdev_dmabuf_priv {
	/* List of exported DMA buffers. */
	struct list_head exp_list;
	/* List of wait objects. */
	struct list_head exp_wait_list;
	/* List of imported DMA buffers. */
	struct list_head imp_list;
	/* This is the lock which protects dma_buf_xxx lists. */
	struct mutex lock;
	/*
	 * We reference this file while exporting dma-bufs, so
	 * the grant device context is not destroyed while there are
	 * external users alive.
	 */
	struct file *filp;
};

/* DMA buffer export support. */

/* Implementation of wait for exported DMA buffer to be released. */

static void dmabuf_exp_release(struct kref *kref);

static struct gntdev_dmabuf_wait_obj *
dmabuf_exp_wait_obj_new(struct gntdev_dmabuf_priv *priv,
			struct gntdev_dmabuf *gntdev_dmabuf)
{
	struct gntdev_dmabuf_wait_obj *obj;

	obj = kzalloc(sizeof(*obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	init_completion(&obj->completion);
	obj->gntdev_dmabuf = gntdev_dmabuf;

	mutex_lock(&priv->lock);
	list_add(&obj->next, &priv->exp_wait_list);
	/* Put our reference and wait for gntdev_dmabuf's release to fire. */
	kref_put(&gntdev_dmabuf->u.exp.refcount, dmabuf_exp_release);
	mutex_unlock(&priv->lock);
	return obj;
}

static void dmabuf_exp_wait_obj_free(struct gntdev_dmabuf_priv *priv,
				     struct gntdev_dmabuf_wait_obj *obj)
{
	mutex_lock(&priv->lock);
	list_del(&obj->next);
	mutex_unlock(&priv->lock);
	kfree(obj);
}

static int dmabuf_exp_wait_obj_wait(struct gntdev_dmabuf_wait_obj *obj,
				    u32 wait_to_ms)
{
	if (wait_for_completion_timeout(&obj->completion,
			msecs_to_jiffies(wait_to_ms)) <= 0)
		return -ETIMEDOUT;

	return 0;
}

static void dmabuf_exp_wait_obj_signal(struct gntdev_dmabuf_priv *priv,
				       struct gntdev_dmabuf *gntdev_dmabuf)
{
	struct gntdev_dmabuf_wait_obj *obj;

	list_for_each_entry(obj, &priv->exp_wait_list, next)
		if (obj->gntdev_dmabuf == gntdev_dmabuf) {
			pr_debug("Found gntdev_dmabuf in the wait list, wake\n");
			complete_all(&obj->completion);
			break;
		}
}

static struct gntdev_dmabuf *
dmabuf_exp_wait_obj_get_dmabuf(struct gntdev_dmabuf_priv *priv, int fd)
{
	struct gntdev_dmabuf *gntdev_dmabuf, *ret = ERR_PTR(-ENOENT);

	mutex_lock(&priv->lock);
	list_for_each_entry(gntdev_dmabuf, &priv->exp_list, next)
		if (gntdev_dmabuf->fd == fd) {
			pr_debug("Found gntdev_dmabuf in the wait list\n");
			kref_get(&gntdev_dmabuf->u.exp.refcount);
			ret = gntdev_dmabuf;
			break;
		}
	mutex_unlock(&priv->lock);
	return ret;
}

static int dmabuf_exp_wait_released(struct gntdev_dmabuf_priv *priv, int fd,
				    int wait_to_ms)
{
	struct gntdev_dmabuf *gntdev_dmabuf;
	struct gntdev_dmabuf_wait_obj *obj;
	int ret;

	pr_debug("Will wait for dma-buf with fd %d\n", fd);
	/*
	 * Try to find the DMA buffer: if not found means that
	 * either the buffer has already been released or file descriptor
	 * provided is wrong.
	 */
	gntdev_dmabuf = dmabuf_exp_wait_obj_get_dmabuf(priv, fd);
	if (IS_ERR(gntdev_dmabuf))
		return PTR_ERR(gntdev_dmabuf);

	/*
	 * gntdev_dmabuf still exists and is reference count locked by us now,
	 * so prepare to wait: allocate wait object and add it to the wait list,
	 * so we can find it on release.
	 */
	obj = dmabuf_exp_wait_obj_new(priv, gntdev_dmabuf);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	ret = dmabuf_exp_wait_obj_wait(obj, wait_to_ms);
	dmabuf_exp_wait_obj_free(priv, obj);
	return ret;
}

/* DMA buffer export support. */

static struct sg_table *
dmabuf_pages_to_sgt(struct page **pages, unsigned int nr_pages)
{
	struct sg_table *sgt;
	int ret;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sg_alloc_table_from_pages(sgt, pages, nr_pages, 0,
					nr_pages << PAGE_SHIFT,
					GFP_KERNEL);
	if (ret)
		goto out;

	return sgt;

out:
	kfree(sgt);
	return ERR_PTR(ret);
}

static int dmabuf_exp_ops_attach(struct dma_buf *dma_buf,
				 struct dma_buf_attachment *attach)
{
	struct gntdev_dmabuf_attachment *gntdev_dmabuf_attach;

	gntdev_dmabuf_attach = kzalloc(sizeof(*gntdev_dmabuf_attach),
				       GFP_KERNEL);
	if (!gntdev_dmabuf_attach)
		return -ENOMEM;

	gntdev_dmabuf_attach->dir = DMA_NONE;
	attach->priv = gntdev_dmabuf_attach;
	return 0;
}

static void dmabuf_exp_ops_detach(struct dma_buf *dma_buf,
				  struct dma_buf_attachment *attach)
{
	struct gntdev_dmabuf_attachment *gntdev_dmabuf_attach = attach->priv;

	if (gntdev_dmabuf_attach) {
		struct sg_table *sgt = gntdev_dmabuf_attach->sgt;

		if (sgt) {
			if (gntdev_dmabuf_attach->dir != DMA_NONE)
				dma_unmap_sg_attrs(attach->dev, sgt->sgl,
						   sgt->nents,
						   gntdev_dmabuf_attach->dir,
						   DMA_ATTR_SKIP_CPU_SYNC);
			sg_free_table(sgt);
		}

		kfree(sgt);
		kfree(gntdev_dmabuf_attach);
		attach->priv = NULL;
	}
}

static struct sg_table *
dmabuf_exp_ops_map_dma_buf(struct dma_buf_attachment *attach,
			   enum dma_data_direction dir)
{
	struct gntdev_dmabuf_attachment *gntdev_dmabuf_attach = attach->priv;
	struct gntdev_dmabuf *gntdev_dmabuf = attach->dmabuf->priv;
	struct sg_table *sgt;

	pr_debug("Mapping %d pages for dev %p\n", gntdev_dmabuf->nr_pages,
		 attach->dev);

	if (dir == DMA_NONE || !gntdev_dmabuf_attach)
		return ERR_PTR(-EINVAL);

	/* Return the cached mapping when possible. */
	if (gntdev_dmabuf_attach->dir == dir)
		return gntdev_dmabuf_attach->sgt;

	/*
	 * Two mappings with different directions for the same attachment are
	 * not allowed.
	 */
	if (gntdev_dmabuf_attach->dir != DMA_NONE)
		return ERR_PTR(-EBUSY);

	sgt = dmabuf_pages_to_sgt(gntdev_dmabuf->pages,
				  gntdev_dmabuf->nr_pages);
	if (!IS_ERR(sgt)) {
		if (!dma_map_sg_attrs(attach->dev, sgt->sgl, sgt->nents, dir,
				      DMA_ATTR_SKIP_CPU_SYNC)) {
			sg_free_table(sgt);
			kfree(sgt);
			sgt = ERR_PTR(-ENOMEM);
		} else {
			gntdev_dmabuf_attach->sgt = sgt;
			gntdev_dmabuf_attach->dir = dir;
		}
	}
	if (IS_ERR(sgt))
		pr_debug("Failed to map sg table for dev %p\n", attach->dev);
	return sgt;
}

static void dmabuf_exp_ops_unmap_dma_buf(struct dma_buf_attachment *attach,
					 struct sg_table *sgt,
					 enum dma_data_direction dir)
{
	/* Not implemented. The unmap is done at dmabuf_exp_ops_detach(). */
}

static void dmabuf_exp_release(struct kref *kref)
{
	struct gntdev_dmabuf *gntdev_dmabuf =
		container_of(kref, struct gntdev_dmabuf, u.exp.refcount);

	dmabuf_exp_wait_obj_signal(gntdev_dmabuf->priv, gntdev_dmabuf);
	list_del(&gntdev_dmabuf->next);
	fput(gntdev_dmabuf->priv->filp);
	kfree(gntdev_dmabuf);
}

static void dmabuf_exp_remove_map(struct gntdev_priv *priv,
				  struct gntdev_grant_map *map)
{
	mutex_lock(&priv->lock);
	list_del(&map->next);
	gntdev_put_map(NULL /* already removed */, map);
	mutex_unlock(&priv->lock);
}

static void dmabuf_exp_ops_release(struct dma_buf *dma_buf)
{
	struct gntdev_dmabuf *gntdev_dmabuf = dma_buf->priv;
	struct gntdev_dmabuf_priv *priv = gntdev_dmabuf->priv;

	dmabuf_exp_remove_map(gntdev_dmabuf->u.exp.priv,
			      gntdev_dmabuf->u.exp.map);
	mutex_lock(&priv->lock);
	kref_put(&gntdev_dmabuf->u.exp.refcount, dmabuf_exp_release);
	mutex_unlock(&priv->lock);
}

static const struct dma_buf_ops dmabuf_exp_ops =  {
	.attach = dmabuf_exp_ops_attach,
	.detach = dmabuf_exp_ops_detach,
	.map_dma_buf = dmabuf_exp_ops_map_dma_buf,
	.unmap_dma_buf = dmabuf_exp_ops_unmap_dma_buf,
	.release = dmabuf_exp_ops_release,
};

struct gntdev_dmabuf_export_args {
	struct gntdev_priv *priv;
	struct gntdev_grant_map *map;
	struct gntdev_dmabuf_priv *dmabuf_priv;
	struct device *dev;
	int count;
	struct page **pages;
	u32 fd;
};

static int dmabuf_exp_from_pages(struct gntdev_dmabuf_export_args *args)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct gntdev_dmabuf *gntdev_dmabuf;
	int ret;

	gntdev_dmabuf = kzalloc(sizeof(*gntdev_dmabuf), GFP_KERNEL);
	if (!gntdev_dmabuf)
		return -ENOMEM;

	kref_init(&gntdev_dmabuf->u.exp.refcount);

	gntdev_dmabuf->priv = args->dmabuf_priv;
	gntdev_dmabuf->nr_pages = args->count;
	gntdev_dmabuf->pages = args->pages;
	gntdev_dmabuf->u.exp.priv = args->priv;
	gntdev_dmabuf->u.exp.map = args->map;

	exp_info.exp_name = KBUILD_MODNAME;
	if (args->dev->driver && args->dev->driver->owner)
		exp_info.owner = args->dev->driver->owner;
	else
		exp_info.owner = THIS_MODULE;
	exp_info.ops = &dmabuf_exp_ops;
	exp_info.size = args->count << PAGE_SHIFT;
	exp_info.flags = O_RDWR;
	exp_info.priv = gntdev_dmabuf;

	gntdev_dmabuf->dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(gntdev_dmabuf->dmabuf)) {
		ret = PTR_ERR(gntdev_dmabuf->dmabuf);
		gntdev_dmabuf->dmabuf = NULL;
		goto fail;
	}

	ret = dma_buf_fd(gntdev_dmabuf->dmabuf, O_CLOEXEC);
	if (ret < 0)
		goto fail;

	gntdev_dmabuf->fd = ret;
	args->fd = ret;

	pr_debug("Exporting DMA buffer with fd %d\n", ret);

	mutex_lock(&args->dmabuf_priv->lock);
	list_add(&gntdev_dmabuf->next, &args->dmabuf_priv->exp_list);
	mutex_unlock(&args->dmabuf_priv->lock);
	get_file(gntdev_dmabuf->priv->filp);
	return 0;

fail:
	if (gntdev_dmabuf->dmabuf)
		dma_buf_put(gntdev_dmabuf->dmabuf);
	kfree(gntdev_dmabuf);
	return ret;
}

static struct gntdev_grant_map *
dmabuf_exp_alloc_backing_storage(struct gntdev_priv *priv, int dmabuf_flags,
				 int count)
{
	struct gntdev_grant_map *map;

	if (unlikely(gntdev_test_page_count(count)))
		return ERR_PTR(-EINVAL);

	if ((dmabuf_flags & GNTDEV_DMA_FLAG_WC) &&
	    (dmabuf_flags & GNTDEV_DMA_FLAG_COHERENT)) {
		pr_debug("Wrong dma-buf flags: 0x%x\n", dmabuf_flags);
		return ERR_PTR(-EINVAL);
	}

	map = gntdev_alloc_map(priv, count, dmabuf_flags);
	if (!map)
		return ERR_PTR(-ENOMEM);

	return map;
}

static int dmabuf_exp_from_refs(struct gntdev_priv *priv, int flags,
				int count, u32 domid, u32 *refs, u32 *fd)
{
	struct gntdev_grant_map *map;
	struct gntdev_dmabuf_export_args args;
	int i, ret;

	map = dmabuf_exp_alloc_backing_storage(priv, flags, count);
	if (IS_ERR(map))
		return PTR_ERR(map);

	for (i = 0; i < count; i++) {
		map->grants[i].domid = domid;
		map->grants[i].ref = refs[i];
	}

	mutex_lock(&priv->lock);
	gntdev_add_map(priv, map);
	mutex_unlock(&priv->lock);

	map->flags |= GNTMAP_host_map;
#if defined(CONFIG_X86)
	map->flags |= GNTMAP_device_map;
#endif

	ret = gntdev_map_grant_pages(map);
	if (ret < 0)
		goto out;

	args.priv = priv;
	args.map = map;
	args.dev = priv->dma_dev;
	args.dmabuf_priv = priv->dmabuf_priv;
	args.count = map->count;
	args.pages = map->pages;
	args.fd = -1; /* Shut up unnecessary gcc warning for i386 */

	ret = dmabuf_exp_from_pages(&args);
	if (ret < 0)
		goto out;

	*fd = args.fd;
	return 0;

out:
	dmabuf_exp_remove_map(priv, map);
	return ret;
}

/* DMA buffer import support. */

static int
dmabuf_imp_grant_foreign_access(struct page **pages, u32 *refs,
				int count, int domid)
{
	grant_ref_t priv_gref_head;
	int i, ret;

	ret = gnttab_alloc_grant_references(count, &priv_gref_head);
	if (ret < 0) {
		pr_debug("Cannot allocate grant references, ret %d\n", ret);
		return ret;
	}

	for (i = 0; i < count; i++) {
		int cur_ref;

		cur_ref = gnttab_claim_grant_reference(&priv_gref_head);
		if (cur_ref < 0) {
			ret = cur_ref;
			pr_debug("Cannot claim grant reference, ret %d\n", ret);
			goto out;
		}

		gnttab_grant_foreign_access_ref(cur_ref, domid,
						xen_page_to_gfn(pages[i]), 0);
		refs[i] = cur_ref;
	}

	return 0;

out:
	gnttab_free_grant_references(priv_gref_head);
	return ret;
}

static void dmabuf_imp_end_foreign_access(u32 *refs, int count)
{
	int i;

	for (i = 0; i < count; i++)
		if (refs[i] != GRANT_INVALID_REF)
			gnttab_end_foreign_access(refs[i], 0, 0UL);
}

static void dmabuf_imp_free_storage(struct gntdev_dmabuf *gntdev_dmabuf)
{
	kfree(gntdev_dmabuf->pages);
	kfree(gntdev_dmabuf->u.imp.refs);
	kfree(gntdev_dmabuf);
}

static struct gntdev_dmabuf *dmabuf_imp_alloc_storage(int count)
{
	struct gntdev_dmabuf *gntdev_dmabuf;
	int i;

	gntdev_dmabuf = kzalloc(sizeof(*gntdev_dmabuf), GFP_KERNEL);
	if (!gntdev_dmabuf)
		goto fail_no_free;

	gntdev_dmabuf->u.imp.refs = kcalloc(count,
					    sizeof(gntdev_dmabuf->u.imp.refs[0]),
					    GFP_KERNEL);
	if (!gntdev_dmabuf->u.imp.refs)
		goto fail;

	gntdev_dmabuf->pages = kcalloc(count,
				       sizeof(gntdev_dmabuf->pages[0]),
				       GFP_KERNEL);
	if (!gntdev_dmabuf->pages)
		goto fail;

	gntdev_dmabuf->nr_pages = count;

	for (i = 0; i < count; i++)
		gntdev_dmabuf->u.imp.refs[i] = GRANT_INVALID_REF;

	return gntdev_dmabuf;

fail:
	dmabuf_imp_free_storage(gntdev_dmabuf);
fail_no_free:
	return ERR_PTR(-ENOMEM);
}

static struct gntdev_dmabuf *
dmabuf_imp_to_refs(struct gntdev_dmabuf_priv *priv, struct device *dev,
		   int fd, int count, int domid)
{
	struct gntdev_dmabuf *gntdev_dmabuf, *ret;
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct sg_page_iter sg_iter;
	int i;

	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf))
		return ERR_CAST(dma_buf);

	gntdev_dmabuf = dmabuf_imp_alloc_storage(count);
	if (IS_ERR(gntdev_dmabuf)) {
		ret = gntdev_dmabuf;
		goto fail_put;
	}

	gntdev_dmabuf->priv = priv;
	gntdev_dmabuf->fd = fd;

	attach = dma_buf_attach(dma_buf, dev);
	if (IS_ERR(attach)) {
		ret = ERR_CAST(attach);
		goto fail_free_obj;
	}

	gntdev_dmabuf->u.imp.attach = attach;

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = ERR_CAST(sgt);
		goto fail_detach;
	}

	/* Check that we have zero offset. */
	if (sgt->sgl->offset) {
		ret = ERR_PTR(-EINVAL);
		pr_debug("DMA buffer has %d bytes offset, user-space expects 0\n",
			 sgt->sgl->offset);
		goto fail_unmap;
	}

	/* Check number of pages that imported buffer has. */
	if (attach->dmabuf->size != gntdev_dmabuf->nr_pages << PAGE_SHIFT) {
		ret = ERR_PTR(-EINVAL);
		pr_debug("DMA buffer has %zu pages, user-space expects %d\n",
			 attach->dmabuf->size, gntdev_dmabuf->nr_pages);
		goto fail_unmap;
	}

	gntdev_dmabuf->u.imp.sgt = sgt;

	/* Now convert sgt to array of pages and check for page validity. */
	i = 0;
	for_each_sg_page(sgt->sgl, &sg_iter, sgt->nents, 0) {
		struct page *page = sg_page_iter_page(&sg_iter);
		/*
		 * Check if page is valid: this can happen if we are given
		 * a page from VRAM or other resources which are not backed
		 * by a struct page.
		 */
		if (!pfn_valid(page_to_pfn(page))) {
			ret = ERR_PTR(-EINVAL);
			goto fail_unmap;
		}

		gntdev_dmabuf->pages[i++] = page;
	}

	ret = ERR_PTR(dmabuf_imp_grant_foreign_access(gntdev_dmabuf->pages,
						      gntdev_dmabuf->u.imp.refs,
						      count, domid));
	if (IS_ERR(ret))
		goto fail_end_access;

	pr_debug("Imported DMA buffer with fd %d\n", fd);

	mutex_lock(&priv->lock);
	list_add(&gntdev_dmabuf->next, &priv->imp_list);
	mutex_unlock(&priv->lock);

	return gntdev_dmabuf;

fail_end_access:
	dmabuf_imp_end_foreign_access(gntdev_dmabuf->u.imp.refs, count);
fail_unmap:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
fail_detach:
	dma_buf_detach(dma_buf, attach);
fail_free_obj:
	dmabuf_imp_free_storage(gntdev_dmabuf);
fail_put:
	dma_buf_put(dma_buf);
	return ret;
}

/*
 * Find the hyper dma-buf by its file descriptor and remove
 * it from the buffer's list.
 */
static struct gntdev_dmabuf *
dmabuf_imp_find_unlink(struct gntdev_dmabuf_priv *priv, int fd)
{
	struct gntdev_dmabuf *q, *gntdev_dmabuf, *ret = ERR_PTR(-ENOENT);

	mutex_lock(&priv->lock);
	list_for_each_entry_safe(gntdev_dmabuf, q, &priv->imp_list, next) {
		if (gntdev_dmabuf->fd == fd) {
			pr_debug("Found gntdev_dmabuf in the import list\n");
			ret = gntdev_dmabuf;
			list_del(&gntdev_dmabuf->next);
			break;
		}
	}
	mutex_unlock(&priv->lock);
	return ret;
}

static int dmabuf_imp_release(struct gntdev_dmabuf_priv *priv, u32 fd)
{
	struct gntdev_dmabuf *gntdev_dmabuf;
	struct dma_buf_attachment *attach;
	struct dma_buf *dma_buf;

	gntdev_dmabuf = dmabuf_imp_find_unlink(priv, fd);
	if (IS_ERR(gntdev_dmabuf))
		return PTR_ERR(gntdev_dmabuf);

	pr_debug("Releasing DMA buffer with fd %d\n", fd);

	dmabuf_imp_end_foreign_access(gntdev_dmabuf->u.imp.refs,
				      gntdev_dmabuf->nr_pages);

	attach = gntdev_dmabuf->u.imp.attach;

	if (gntdev_dmabuf->u.imp.sgt)
		dma_buf_unmap_attachment(attach, gntdev_dmabuf->u.imp.sgt,
					 DMA_BIDIRECTIONAL);
	dma_buf = attach->dmabuf;
	dma_buf_detach(attach->dmabuf, attach);
	dma_buf_put(dma_buf);

	dmabuf_imp_free_storage(gntdev_dmabuf);
	return 0;
}

static void dmabuf_imp_release_all(struct gntdev_dmabuf_priv *priv)
{
	struct gntdev_dmabuf *q, *gntdev_dmabuf;

	list_for_each_entry_safe(gntdev_dmabuf, q, &priv->imp_list, next)
		dmabuf_imp_release(priv, gntdev_dmabuf->fd);
}

/* DMA buffer IOCTL support. */

long gntdev_ioctl_dmabuf_exp_from_refs(struct gntdev_priv *priv, int use_ptemod,
				       struct ioctl_gntdev_dmabuf_exp_from_refs __user *u)
{
	struct ioctl_gntdev_dmabuf_exp_from_refs op;
	u32 *refs;
	long ret;

	if (use_ptemod) {
		pr_debug("Cannot provide dma-buf: use_ptemode %d\n",
			 use_ptemod);
		return -EINVAL;
	}

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;

	if (unlikely(gntdev_test_page_count(op.count)))
		return -EINVAL;

	refs = kcalloc(op.count, sizeof(*refs), GFP_KERNEL);
	if (!refs)
		return -ENOMEM;

	if (copy_from_user(refs, u->refs, sizeof(*refs) * op.count) != 0) {
		ret = -EFAULT;
		goto out;
	}

	ret = dmabuf_exp_from_refs(priv, op.flags, op.count,
				   op.domid, refs, &op.fd);
	if (ret)
		goto out;

	if (copy_to_user(u, &op, sizeof(op)) != 0)
		ret = -EFAULT;

out:
	kfree(refs);
	return ret;
}

long gntdev_ioctl_dmabuf_exp_wait_released(struct gntdev_priv *priv,
					   struct ioctl_gntdev_dmabuf_exp_wait_released __user *u)
{
	struct ioctl_gntdev_dmabuf_exp_wait_released op;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;

	return dmabuf_exp_wait_released(priv->dmabuf_priv, op.fd,
					op.wait_to_ms);
}

long gntdev_ioctl_dmabuf_imp_to_refs(struct gntdev_priv *priv,
				     struct ioctl_gntdev_dmabuf_imp_to_refs __user *u)
{
	struct ioctl_gntdev_dmabuf_imp_to_refs op;
	struct gntdev_dmabuf *gntdev_dmabuf;
	long ret;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;

	if (unlikely(gntdev_test_page_count(op.count)))
		return -EINVAL;

	gntdev_dmabuf = dmabuf_imp_to_refs(priv->dmabuf_priv,
					   priv->dma_dev, op.fd,
					   op.count, op.domid);
	if (IS_ERR(gntdev_dmabuf))
		return PTR_ERR(gntdev_dmabuf);

	if (copy_to_user(u->refs, gntdev_dmabuf->u.imp.refs,
			 sizeof(*u->refs) * op.count) != 0) {
		ret = -EFAULT;
		goto out_release;
	}
	return 0;

out_release:
	dmabuf_imp_release(priv->dmabuf_priv, op.fd);
	return ret;
}

long gntdev_ioctl_dmabuf_imp_release(struct gntdev_priv *priv,
				     struct ioctl_gntdev_dmabuf_imp_release __user *u)
{
	struct ioctl_gntdev_dmabuf_imp_release op;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;

	return dmabuf_imp_release(priv->dmabuf_priv, op.fd);
}

struct gntdev_dmabuf_priv *gntdev_dmabuf_init(struct file *filp)
{
	struct gntdev_dmabuf_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return ERR_PTR(-ENOMEM);

	mutex_init(&priv->lock);
	INIT_LIST_HEAD(&priv->exp_list);
	INIT_LIST_HEAD(&priv->exp_wait_list);
	INIT_LIST_HEAD(&priv->imp_list);

	priv->filp = filp;

	return priv;
}

void gntdev_dmabuf_fini(struct gntdev_dmabuf_priv *priv)
{
	dmabuf_imp_release_all(priv);
	kfree(priv);
}
