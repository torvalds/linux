// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 */

#include <linux/slab.h>
#include <linux/dma-buf.h>
#undef CONFIG_DMABUF_CACHE
#include <linux/dma-buf-cache.h>

/* NOTE: dma-buf-cache APIs are not irq safe, please DO NOT run in irq context !! */

struct dma_buf_cache_list {
	struct list_head head;
};

struct dma_buf_cache {
	struct list_head list;
	struct dma_buf_attachment *attach;
	enum dma_data_direction direction;
	struct sg_table *sg_table;
};

static int dma_buf_cache_destructor(struct dma_buf *dmabuf, void *dtor_data)
{
	struct dma_buf_cache_list *data;
	struct dma_buf_cache *cache, *tmp;

	mutex_lock(&dmabuf->cache_lock);

	data = dmabuf->dtor_data;

	list_for_each_entry_safe(cache, tmp, &data->head, list) {
		if (!IS_ERR_OR_NULL(cache->sg_table))
			dma_buf_unmap_attachment(cache->attach,
						 cache->sg_table,
						 cache->direction);

		dma_buf_detach(dmabuf, cache->attach);
		list_del(&cache->list);
		kfree(cache);
	}

	mutex_unlock(&dmabuf->cache_lock);

	kfree(data);
	return 0;
}

static struct dma_buf_cache *
dma_buf_cache_get_cache(struct dma_buf_attachment *attach)
{
	struct dma_buf_cache_list *data;
	struct dma_buf_cache *cache;
	struct dma_buf *dmabuf = attach->dmabuf;

	if (dmabuf->dtor != dma_buf_cache_destructor)
		return NULL;

	data = dmabuf->dtor_data;

	list_for_each_entry(cache, &data->head, list) {
		if (cache->attach == attach)
			return cache;
	}

	return NULL;
}

void dma_buf_cache_detach(struct dma_buf *dmabuf,
			  struct dma_buf_attachment *attach)
{
	struct dma_buf_cache *cache;

	mutex_lock(&dmabuf->cache_lock);

	cache = dma_buf_cache_get_cache(attach);
	if (!cache)
		dma_buf_detach(dmabuf, attach);

	mutex_unlock(&dmabuf->cache_lock);
}
EXPORT_SYMBOL(dma_buf_cache_detach);

struct dma_buf_attachment *dma_buf_cache_attach(struct dma_buf *dmabuf,
						struct device *dev)
{
	struct dma_buf_attachment *attach;
	struct dma_buf_cache_list *data;
	struct dma_buf_cache *cache;

	mutex_lock(&dmabuf->cache_lock);

	if (!dmabuf->dtor) {
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data) {
			attach = ERR_PTR(-ENOMEM);
			goto err_data;
		}
		INIT_LIST_HEAD(&data->head);
		dma_buf_set_destructor(dmabuf, dma_buf_cache_destructor, data);
	}

	if (dmabuf->dtor && dmabuf->dtor != dma_buf_cache_destructor) {
		attach = dma_buf_attach(dmabuf, dev);
		goto attach_done;
	}

	data = dmabuf->dtor_data;

	list_for_each_entry(cache, &data->head, list) {
		if (cache->attach->dev == dev) {
			/* Already attached */
			attach = cache->attach;
			goto attach_done;
		}
	}

	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (!cache) {
		attach = ERR_PTR(-ENOMEM);
		goto err_cache;
	}
	/* Cache attachment */
	attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR_OR_NULL(attach))
		goto err_attach;

	cache->attach = attach;
	list_add(&cache->list, &data->head);

attach_done:
	mutex_unlock(&dmabuf->cache_lock);
	return attach;

err_attach:
	kfree(cache);
err_cache:
	kfree(data);
	dma_buf_set_destructor(dmabuf, NULL, NULL);
err_data:
	mutex_unlock(&dmabuf->cache_lock);
	return attach;
}
EXPORT_SYMBOL(dma_buf_cache_attach);

void dma_buf_cache_unmap_attachment(struct dma_buf_attachment *attach,
				    struct sg_table *sg_table,
				    enum dma_data_direction direction)
{
	struct dma_buf *dmabuf = attach->dmabuf;
	struct dma_buf_cache *cache;

	mutex_lock(&dmabuf->cache_lock);

	cache = dma_buf_cache_get_cache(attach);
	if (!cache)
		dma_buf_unmap_attachment(attach, sg_table, direction);

	mutex_unlock(&dmabuf->cache_lock);
}
EXPORT_SYMBOL(dma_buf_cache_unmap_attachment);

struct sg_table *dma_buf_cache_map_attachment(struct dma_buf_attachment *attach,
					      enum dma_data_direction direction)
{
	struct dma_buf *dmabuf = attach->dmabuf;
	struct dma_buf_cache *cache;
	struct sg_table *sg_table;

	mutex_lock(&dmabuf->cache_lock);

	cache = dma_buf_cache_get_cache(attach);
	if (!cache) {
		sg_table = dma_buf_map_attachment(attach, direction);
		goto map_done;
	}
	if (cache->sg_table) {
		/* Already mapped */
		if (cache->direction == direction) {
			sg_table = cache->sg_table;
			goto map_done;
		}
		/* Different directions */
		dma_buf_unmap_attachment(attach, cache->sg_table,
					 cache->direction);
	}

	/* Cache map */
	sg_table = dma_buf_map_attachment(attach, direction);
	cache->sg_table = sg_table;
	cache->direction = direction;

map_done:
	mutex_unlock(&dmabuf->cache_lock);
	return sg_table;
}
EXPORT_SYMBOL(dma_buf_cache_map_attachment);
