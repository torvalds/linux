// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Linaro Limited
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/genalloc.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/tee_core.h>
#include <linux/xarray.h>

#include "tee_private.h"

struct tee_dma_heap {
	struct dma_heap *heap;
	enum tee_dma_heap_id id;
	struct kref kref;
	struct tee_protmem_pool *pool;
	struct tee_device *teedev;
	bool shutting_down;
	/* Protects pool, teedev, and shutting_down above */
	struct mutex mu;
};

struct tee_heap_buffer {
	struct tee_dma_heap *heap;
	size_t size;
	size_t offs;
	struct sg_table table;
};

struct tee_heap_attachment {
	struct sg_table table;
	struct device *dev;
};

struct tee_protmem_static_pool {
	struct tee_protmem_pool pool;
	struct gen_pool *gen_pool;
	phys_addr_t pa_base;
};

#if IS_ENABLED(CONFIG_TEE_DMABUF_HEAPS)
static DEFINE_XARRAY_ALLOC(tee_dma_heap);

static void tee_heap_release(struct kref *kref)
{
	struct tee_dma_heap *h = container_of(kref, struct tee_dma_heap, kref);

	h->pool->ops->destroy_pool(h->pool);
	tee_device_put(h->teedev);
	h->pool = NULL;
	h->teedev = NULL;
}

static void put_tee_heap(struct tee_dma_heap *h)
{
	kref_put(&h->kref, tee_heap_release);
}

static void get_tee_heap(struct tee_dma_heap *h)
{
	kref_get(&h->kref);
}

static int copy_sg_table(struct sg_table *dst, struct sg_table *src)
{
	struct scatterlist *dst_sg;
	struct scatterlist *src_sg;
	int ret;
	int i;

	ret = sg_alloc_table(dst, src->orig_nents, GFP_KERNEL);
	if (ret)
		return ret;

	dst_sg = dst->sgl;
	for_each_sgtable_sg(src, src_sg, i) {
		sg_set_page(dst_sg, sg_page(src_sg), src_sg->length,
			    src_sg->offset);
		dst_sg = sg_next(dst_sg);
	}

	return 0;
}

static int tee_heap_attach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attachment)
{
	struct tee_heap_buffer *buf = dmabuf->priv;
	struct tee_heap_attachment *a;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = copy_sg_table(&a->table, &buf->table);
	if (ret) {
		kfree(a);
		return ret;
	}

	a->dev = attachment->dev;
	attachment->priv = a;

	return 0;
}

static void tee_heap_detach(struct dma_buf *dmabuf,
			    struct dma_buf_attachment *attachment)
{
	struct tee_heap_attachment *a = attachment->priv;

	sg_free_table(&a->table);
	kfree(a);
}

static struct sg_table *
tee_heap_map_dma_buf(struct dma_buf_attachment *attachment,
		     enum dma_data_direction direction)
{
	struct tee_heap_attachment *a = attachment->priv;
	int ret;

	ret = dma_map_sgtable(attachment->dev, &a->table, direction,
			      DMA_ATTR_SKIP_CPU_SYNC);
	if (ret)
		return ERR_PTR(ret);

	return &a->table;
}

static void tee_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				   struct sg_table *table,
				   enum dma_data_direction direction)
{
	struct tee_heap_attachment *a = attachment->priv;

	WARN_ON(&a->table != table);

	dma_unmap_sgtable(attachment->dev, table, direction,
			  DMA_ATTR_SKIP_CPU_SYNC);
}

static void tee_heap_buf_free(struct dma_buf *dmabuf)
{
	struct tee_heap_buffer *buf = dmabuf->priv;

	buf->heap->pool->ops->free(buf->heap->pool, &buf->table);
	mutex_lock(&buf->heap->mu);
	put_tee_heap(buf->heap);
	mutex_unlock(&buf->heap->mu);
	kfree(buf);
}

static const struct dma_buf_ops tee_heap_buf_ops = {
	.attach = tee_heap_attach,
	.detach = tee_heap_detach,
	.map_dma_buf = tee_heap_map_dma_buf,
	.unmap_dma_buf = tee_heap_unmap_dma_buf,
	.release = tee_heap_buf_free,
};

static struct dma_buf *tee_dma_heap_alloc(struct dma_heap *heap,
					  unsigned long len, u32 fd_flags,
					  u64 heap_flags)
{
	struct tee_dma_heap *h = dma_heap_get_drvdata(heap);
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct tee_device *teedev = NULL;
	struct tee_heap_buffer *buf;
	struct tee_protmem_pool *pool;
	struct dma_buf *dmabuf;
	int rc;

	mutex_lock(&h->mu);
	if (h->teedev) {
		teedev = h->teedev;
		pool = h->pool;
		get_tee_heap(h);
	}
	mutex_unlock(&h->mu);

	if (!teedev)
		return ERR_PTR(-EINVAL);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf) {
		dmabuf = ERR_PTR(-ENOMEM);
		goto err;
	}
	buf->size = len;
	buf->heap = h;

	rc = pool->ops->alloc(pool, &buf->table, len, &buf->offs);
	if (rc) {
		dmabuf = ERR_PTR(rc);
		goto err_kfree;
	}

	exp_info.ops = &tee_heap_buf_ops;
	exp_info.size = len;
	exp_info.priv = buf;
	exp_info.flags = fd_flags;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf))
		goto err_protmem_free;

	return dmabuf;

err_protmem_free:
	pool->ops->free(pool, &buf->table);
err_kfree:
	kfree(buf);
err:
	mutex_lock(&h->mu);
	put_tee_heap(h);
	mutex_unlock(&h->mu);
	return dmabuf;
}

static const struct dma_heap_ops tee_dma_heap_ops = {
	.allocate = tee_dma_heap_alloc,
};

static const char *heap_id_2_name(enum tee_dma_heap_id id)
{
	switch (id) {
	case TEE_DMA_HEAP_SECURE_VIDEO_PLAY:
		return "protected,secure-video";
	case TEE_DMA_HEAP_TRUSTED_UI:
		return "protected,trusted-ui";
	case TEE_DMA_HEAP_SECURE_VIDEO_RECORD:
		return "protected,secure-video-record";
	default:
		return NULL;
	}
}

static int alloc_dma_heap(struct tee_device *teedev, enum tee_dma_heap_id id,
			  struct tee_protmem_pool *pool)
{
	struct dma_heap_export_info exp_info = {
		.ops = &tee_dma_heap_ops,
		.name = heap_id_2_name(id),
	};
	struct tee_dma_heap *h;
	int rc;

	if (!exp_info.name)
		return -EINVAL;

	if (xa_reserve(&tee_dma_heap, id, GFP_KERNEL)) {
		if (!xa_load(&tee_dma_heap, id))
			return -EEXIST;
		return -ENOMEM;
	}

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->id = id;
	kref_init(&h->kref);
	h->teedev = teedev;
	h->pool = pool;
	mutex_init(&h->mu);

	exp_info.priv = h;
	h->heap = dma_heap_add(&exp_info);
	if (IS_ERR(h->heap)) {
		rc = PTR_ERR(h->heap);
		kfree(h);

		return rc;
	}

	/* "can't fail" due to the call to xa_reserve() above */
	return WARN_ON(xa_is_err(xa_store(&tee_dma_heap, id, h, GFP_KERNEL)));
}

int tee_device_register_dma_heap(struct tee_device *teedev,
				 enum tee_dma_heap_id id,
				 struct tee_protmem_pool *pool)
{
	struct tee_dma_heap *h;
	int rc;

	if (!tee_device_get(teedev))
		return -EINVAL;

	h = xa_load(&tee_dma_heap, id);
	if (h) {
		mutex_lock(&h->mu);
		if (h->teedev) {
			rc = -EBUSY;
		} else {
			kref_init(&h->kref);
			h->shutting_down = false;
			h->teedev = teedev;
			h->pool = pool;
			rc = 0;
		}
		mutex_unlock(&h->mu);
	} else {
		rc = alloc_dma_heap(teedev, id, pool);
	}

	if (rc) {
		tee_device_put(teedev);
		dev_err(&teedev->dev, "can't register DMA heap id %d (%s)\n",
			id, heap_id_2_name(id));
	}

	return rc;
}
EXPORT_SYMBOL_GPL(tee_device_register_dma_heap);

void tee_device_put_all_dma_heaps(struct tee_device *teedev)
{
	struct tee_dma_heap *h;
	u_long i;

	xa_for_each(&tee_dma_heap, i, h) {
		if (h) {
			mutex_lock(&h->mu);
			if (h->teedev == teedev && !h->shutting_down) {
				h->shutting_down = true;
				put_tee_heap(h);
			}
			mutex_unlock(&h->mu);
		}
	}
}
EXPORT_SYMBOL_GPL(tee_device_put_all_dma_heaps);

int tee_heap_update_from_dma_buf(struct tee_device *teedev,
				 struct dma_buf *dmabuf, size_t *offset,
				 struct tee_shm *shm,
				 struct tee_shm **parent_shm)
{
	struct tee_heap_buffer *buf;
	int rc;

	/* The DMA-buf must be from our heap */
	if (dmabuf->ops != &tee_heap_buf_ops)
		return -EINVAL;

	buf = dmabuf->priv;
	/* The buffer must be from the same teedev */
	if (buf->heap->teedev != teedev)
		return -EINVAL;

	shm->size = buf->size;

	rc = buf->heap->pool->ops->update_shm(buf->heap->pool, &buf->table,
					      buf->offs, shm, parent_shm);
	if (!rc && *parent_shm)
		*offset = buf->offs;

	return rc;
}
#else
int tee_device_register_dma_heap(struct tee_device *teedev __always_unused,
				 enum tee_dma_heap_id id __always_unused,
				 struct tee_protmem_pool *pool __always_unused)
{
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(tee_device_register_dma_heap);

void
tee_device_put_all_dma_heaps(struct tee_device *teedev __always_unused)
{
}
EXPORT_SYMBOL_GPL(tee_device_put_all_dma_heaps);

int tee_heap_update_from_dma_buf(struct tee_device *teedev __always_unused,
				 struct dma_buf *dmabuf __always_unused,
				 size_t *offset __always_unused,
				 struct tee_shm *shm __always_unused,
				 struct tee_shm **parent_shm __always_unused)
{
	return -EINVAL;
}
#endif

static struct tee_protmem_static_pool *
to_protmem_static_pool(struct tee_protmem_pool *pool)
{
	return container_of(pool, struct tee_protmem_static_pool, pool);
}

static int protmem_pool_op_static_alloc(struct tee_protmem_pool *pool,
					struct sg_table *sgt, size_t size,
					size_t *offs)
{
	struct tee_protmem_static_pool *stp = to_protmem_static_pool(pool);
	phys_addr_t pa;
	int ret;

	pa = gen_pool_alloc(stp->gen_pool, size);
	if (!pa)
		return -ENOMEM;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (ret) {
		gen_pool_free(stp->gen_pool, pa, size);
		return ret;
	}

	sg_set_page(sgt->sgl, phys_to_page(pa), size, 0);
	*offs = pa - stp->pa_base;

	return 0;
}

static void protmem_pool_op_static_free(struct tee_protmem_pool *pool,
					struct sg_table *sgt)
{
	struct tee_protmem_static_pool *stp = to_protmem_static_pool(pool);
	struct scatterlist *sg;
	int i;

	for_each_sgtable_sg(sgt, sg, i)
		gen_pool_free(stp->gen_pool, sg_phys(sg), sg->length);
	sg_free_table(sgt);
}

static int protmem_pool_op_static_update_shm(struct tee_protmem_pool *pool,
					     struct sg_table *sgt, size_t offs,
					     struct tee_shm *shm,
					     struct tee_shm **parent_shm)
{
	struct tee_protmem_static_pool *stp = to_protmem_static_pool(pool);

	shm->paddr = stp->pa_base + offs;
	*parent_shm = NULL;

	return 0;
}

static void protmem_pool_op_static_destroy_pool(struct tee_protmem_pool *pool)
{
	struct tee_protmem_static_pool *stp = to_protmem_static_pool(pool);

	gen_pool_destroy(stp->gen_pool);
	kfree(stp);
}

static struct tee_protmem_pool_ops protmem_pool_ops_static = {
	.alloc = protmem_pool_op_static_alloc,
	.free = protmem_pool_op_static_free,
	.update_shm = protmem_pool_op_static_update_shm,
	.destroy_pool = protmem_pool_op_static_destroy_pool,
};

struct tee_protmem_pool *tee_protmem_static_pool_alloc(phys_addr_t paddr,
						       size_t size)
{
	const size_t page_mask = PAGE_SIZE - 1;
	struct tee_protmem_static_pool *stp;
	int rc;

	/* Check it's page aligned */
	if ((paddr | size) & page_mask)
		return ERR_PTR(-EINVAL);

	if (!pfn_valid(PHYS_PFN(paddr)))
		return ERR_PTR(-EINVAL);

	stp = kzalloc(sizeof(*stp), GFP_KERNEL);
	if (!stp)
		return ERR_PTR(-ENOMEM);

	stp->gen_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!stp->gen_pool) {
		rc = -ENOMEM;
		goto err_free;
	}

	rc = gen_pool_add(stp->gen_pool, paddr, size, -1);
	if (rc)
		goto err_free_pool;

	stp->pool.ops = &protmem_pool_ops_static;
	stp->pa_base = paddr;
	return &stp->pool;

err_free_pool:
	gen_pool_destroy(stp->gen_pool);
err_free:
	kfree(stp);

	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(tee_protmem_static_pool_alloc);
