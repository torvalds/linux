// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 */

#include <linux/slab.h>
#include <linux/dma-buf.h>
#undef CONFIG_DMABUF_CACHE
#include <linux/dma-buf-cache.h>

struct dma_buf_cache_list {
	struct list_head head;
	struct mutex lock;
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

	data = dmabuf->dtor_data;

	mutex_lock(&data->lock);
	list_for_each_entry_safe(cache, tmp, &data->head, list) {
		if (!IS_ERR_OR_NULL(cache->sg_table))
			dma_buf_unmap_attachment(cache->attach,
						 cache->sg_table,
						 cache->direction);

		dma_buf_detach(dmabuf, cache->attach);
		list_del(&cache->list);
		kfree(cache);

	}
	mutex_unlock(&data->lock);

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

	mutex_lock(&data->lock);
	list_for_each_entry(cache, &data->head, list) {
		if (cache->attach == attach) {
			mutex_unlock(&data->lock);
			return cache;
		}
	}
	mutex_unlock(&data->lock);

	return NULL;
}

void dma_buf_cache_detach(struct dma_buf *dmabuf,
			  struct dma_buf_attachment *attach)
{
	struct dma_buf_cache *cache;

	cache = dma_buf_cache_get_cache(attach);
	if (!cache)
		dma_buf_detach(dmabuf, attach);
}
EXPORT_SYMBOL(dma_buf_cache_detach);

struct dma_buf_attachment *dma_buf_cache_attach(struct dma_buf *dmabuf,
						struct device *dev)
{
	struct dma_buf_attachment *attach;
	struct dma_buf_cache_list *data;
	struct dma_buf_cache *cache;

	if (!dmabuf->dtor) {
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data)
			return ERR_PTR(-ENOMEM);

		mutex_init(&data->lock);
		INIT_LIST_HEAD(&data->head);

		dma_buf_set_destructor(dmabuf, dma_buf_cache_destructor, data);
	}

	if (dmabuf->dtor && dmabuf->dtor != dma_buf_cache_destructor)
		return dma_buf_attach(dmabuf, dev);

	data = dmabuf->dtor_data;

	mutex_lock(&data->lock);
	list_for_each_entry(cache, &data->head, list) {
		if (cache->attach->dev == dev) {
			/* Already attached */
			mutex_unlock(&data->lock);
			return cache->attach;
		}
	}
	mutex_unlock(&data->lock);

	cache = kzalloc(sizeof(*cache), GFP_KERNEL);
	if (!cache)
		return ERR_PTR(-ENOMEM);

	/* Cache attachment */
	attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR_OR_NULL(attach)) {
		kfree(cache);
		return attach;
	}

	cache->attach = attach;
	mutex_lock(&data->lock);
	list_add(&cache->list, &data->head);
	mutex_unlock(&data->lock);

	return cache->attach;
}
EXPORT_SYMBOL(dma_buf_cache_attach);

void dma_buf_cache_unmap_attachment(struct dma_buf_attachment *attach,
				    struct sg_table *sg_table,
				    enum dma_data_direction direction)
{
	struct dma_buf_cache *cache;

	cache = dma_buf_cache_get_cache(attach);
	if (!cache)
		dma_buf_unmap_attachment(attach, sg_table, direction);
}
EXPORT_SYMBOL(dma_buf_cache_unmap_attachment);

struct sg_table *dma_buf_cache_map_attachment(struct dma_buf_attachment *attach,
					      enum dma_data_direction direction)
{
	struct dma_buf_cache *cache;

	cache = dma_buf_cache_get_cache(attach);
	if (!cache)
		return dma_buf_map_attachment(attach, direction);

	if (cache->sg_table) {
		/* Already mapped */
		if (cache->direction == direction)
			return cache->sg_table;

		/* Different directions */
		dma_buf_unmap_attachment(attach, cache->sg_table,
					 cache->direction);

	}

	/* Cache map */
	cache->sg_table = dma_buf_map_attachment(attach, direction);
	cache->direction = direction;

	return cache->sg_table;
}
EXPORT_SYMBOL(dma_buf_cache_map_attachment);
