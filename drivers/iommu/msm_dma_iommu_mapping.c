// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2019, 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/dma-map-ops.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <asm/barrier.h>

#include <linux/msm_dma_iommu_mapping.h>
#include <linux/qcom-dma-mapping.h>

/**
 * struct msm_iommu_map - represents a mapping of an ion buffer to an iommu
 * @lnode - list node to exist in the buffer's list of iommu mappings
 * @dev - Device this is mapped to. Used as key
 * @sgl - The scatterlist for this mapping
 * @nents - Number of entries in sgl
 * @dir - The direction for the map.
 * @meta - Backpointer to the meta this guy belongs to.
 * @ref - for reference counting this mapping
 * @attrs - dma mapping attributes
 * @buf_start_addr - address of start of buffer
 *
 * Represents a mapping of one dma_buf buffer to a particular device
 * and address range. There may exist other mappings of this buffer in
 * different devices. All mappings will have the same cacheability and security.
 */
struct msm_iommu_map {
	struct list_head lnode;
	struct rb_node node;
	struct device *dev;
	struct scatterlist *sgl;
	unsigned int nents;
	enum dma_data_direction dir;
	struct msm_iommu_meta *meta;
	struct kref ref;
	unsigned long attrs;
	dma_addr_t buf_start_addr;
};

struct msm_iommu_meta {
	struct rb_node node;
	struct list_head iommu_maps;
	struct kref ref;
	struct mutex lock;
	void *buffer;
};

static struct rb_root iommu_root;
static DEFINE_MUTEX(msm_iommu_map_mutex);

static void msm_iommu_meta_add(struct msm_iommu_meta *meta)
{
	struct rb_root *root = &iommu_root;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct msm_iommu_meta *entry;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct msm_iommu_meta, node);

		if (meta->buffer < entry->buffer)
			p = &(*p)->rb_left;
		else if (meta->buffer > entry->buffer)
			p = &(*p)->rb_right;
		else
			pr_err("%s: dma_buf %pK already exists\n", __func__,
			       entry->buffer);
	}

	rb_link_node(&meta->node, parent, p);
	rb_insert_color(&meta->node, root);
}

static struct msm_iommu_meta *msm_iommu_meta_lookup(void *buffer)
{
	struct rb_root *root = &iommu_root;
	struct rb_node **p = &root->rb_node;
	struct rb_node *parent = NULL;
	struct msm_iommu_meta *entry = NULL;

	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct msm_iommu_meta, node);

		if (buffer < entry->buffer)
			p = &(*p)->rb_left;
		else if (buffer > entry->buffer)
			p = &(*p)->rb_right;
		else
			return entry;
	}

	return NULL;
}

static void msm_iommu_add(struct msm_iommu_meta *meta,
			  struct msm_iommu_map *iommu)
{
	INIT_LIST_HEAD(&iommu->lnode);
	list_add(&iommu->lnode, &meta->iommu_maps);
}


static struct msm_iommu_map *msm_iommu_lookup(struct msm_iommu_meta *meta,
					      struct device *dev)
{
	struct msm_iommu_map *entry;

	list_for_each_entry(entry, &meta->iommu_maps, lnode) {
		if (entry->dev == dev)
			return entry;
	}

	return NULL;
}

static struct msm_iommu_meta *msm_iommu_meta_create(struct dma_buf *dma_buf)
{
	struct msm_iommu_meta *meta;

	meta = kzalloc(sizeof(*meta), GFP_KERNEL);

	if (!meta)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&meta->iommu_maps);
	meta->buffer = dma_buf->priv;
	kref_init(&meta->ref);
	mutex_init(&meta->lock);
	msm_iommu_meta_add(meta);

	return meta;
}

static void msm_iommu_meta_put(struct msm_iommu_meta *meta);

static struct scatterlist *clone_sgl(struct scatterlist *sg, int nents)
{
	struct scatterlist *next, *s;
	int i;
	struct sg_table table;

	if (sg_alloc_table(&table, nents, GFP_KERNEL))
		return NULL;
	next = table.sgl;
	for_each_sg(sg, s, nents, i) {
		*next = *s;
		next = sg_next(next);
	}
	return table.sgl;
}

static inline int __msm_dma_map_sg(struct device *dev, struct scatterlist *sg,
				   int nents, enum dma_data_direction dir,
				   struct dma_buf *dma_buf,
				   unsigned long attrs)
{
	struct msm_iommu_map *iommu_map;
	struct msm_iommu_meta *iommu_meta = NULL;
	int ret = 0;
	bool extra_meta_ref_taken = false;
	int late_unmap = !(attrs & DMA_ATTR_NO_DELAYED_UNMAP);

	mutex_lock(&msm_iommu_map_mutex);
	iommu_meta = msm_iommu_meta_lookup(dma_buf->priv);

	if (!iommu_meta) {
		iommu_meta = msm_iommu_meta_create(dma_buf);

		if (IS_ERR(iommu_meta)) {
			mutex_unlock(&msm_iommu_map_mutex);
			ret = PTR_ERR(iommu_meta);
			goto out;
		}
		if (late_unmap) {
			kref_get(&iommu_meta->ref);
			extra_meta_ref_taken = true;
		}
	} else {
		kref_get(&iommu_meta->ref);
	}

	mutex_unlock(&msm_iommu_map_mutex);

	mutex_lock(&iommu_meta->lock);
	iommu_map = msm_iommu_lookup(iommu_meta, dev);
	if (!iommu_map) {
		iommu_map = kmalloc(sizeof(*iommu_map), GFP_KERNEL);

		if (!iommu_map) {
			ret = -ENOMEM;
			goto out_unlock;
		}

		ret = dma_map_sg_attrs(dev, sg, nents, dir, attrs);
		if (!ret) {
			kfree(iommu_map);
			goto out_unlock;
		}

		iommu_map->sgl = clone_sgl(sg, nents);
		if (!iommu_map->sgl) {
			kfree(iommu_map);
			ret = -ENOMEM;
			goto out_unlock;
		}
		iommu_map->nents = nents;
		iommu_map->dev = dev;
		iommu_map->dir = dir;
		iommu_map->attrs = attrs;
		iommu_map->buf_start_addr = sg_phys(sg);

		kref_init(&iommu_map->ref);
		if (late_unmap)
			kref_get(&iommu_map->ref);
		iommu_map->meta = iommu_meta;
		msm_iommu_add(iommu_meta, iommu_map);

	} else {
		if (nents == iommu_map->nents &&
		    dir == iommu_map->dir &&
		    (attrs & ~DMA_ATTR_SKIP_CPU_SYNC) ==
		    (iommu_map->attrs & ~DMA_ATTR_SKIP_CPU_SYNC) &&
		    sg_phys(sg) == iommu_map->buf_start_addr) {
			struct scatterlist *sg_tmp = sg;
			struct scatterlist *map_sg;
			int i;

			for_each_sg(iommu_map->sgl, map_sg, nents, i) {
				sg_dma_address(sg_tmp) = sg_dma_address(map_sg);
				sg_dma_len(sg_tmp) = sg_dma_len(map_sg);
				if (sg_dma_len(map_sg) == 0)
					break;

				sg_tmp = sg_next(sg_tmp);
				if (sg_tmp == NULL)
					break;
			}

			kref_get(&iommu_map->ref);

			if ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0)
				dma_sync_sg_for_device(dev, iommu_map->sgl,
					iommu_map->nents, iommu_map->dir);

			if (dev_is_dma_coherent(dev))
				/*
				 * Ensure all outstanding changes for coherent
				 * buffers are applied to the cache before any
				 * DMA occurs.
				 */
				dmb(ish);
			ret = nents;
		} else {
			bool start_diff = (sg_phys(sg) !=
					   iommu_map->buf_start_addr);

			dev_err(dev, "lazy map request differs:\n"
				"req dir:%d, original dir:%d\n"
				"req nents:%d, original nents:%d\n"
				"req map attrs:%lu, original map attrs:%lu\n"
				"req buffer start address differs:%d\n",
				dir, iommu_map->dir, nents,
				iommu_map->nents, attrs, iommu_map->attrs,
				start_diff);
			ret = -EINVAL;
		}
	}
	mutex_unlock(&iommu_meta->lock);
	return ret;

out_unlock:
	mutex_unlock(&iommu_meta->lock);
out:
	if (!IS_ERR(iommu_meta)) {
		if (extra_meta_ref_taken)
			msm_iommu_meta_put(iommu_meta);
		msm_iommu_meta_put(iommu_meta);
	}
	return ret;

}

/*
 * We are not taking a reference to the dma_buf here. It is expected that
 * clients hold reference to the dma_buf until they are done with mapping and
 * unmapping.
 */
int msm_dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
		   enum dma_data_direction dir, struct dma_buf *dma_buf,
		   unsigned long attrs)
{
	int ret;

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("%s: dev pointer is invalid\n", __func__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(sg)) {
		pr_err("%s: sg table pointer is invalid\n", __func__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(dma_buf)) {
		pr_err("%s: dma_buf pointer is invalid\n", __func__);
		return -EINVAL;
	}

	ret = __msm_dma_map_sg(dev, sg, nents, dir, dma_buf, attrs);

	return ret;
}
EXPORT_SYMBOL(msm_dma_map_sg_attrs);

static void msm_iommu_meta_destroy(struct kref *kref)
{
	struct msm_iommu_meta *meta = container_of(kref, struct msm_iommu_meta,
						ref);

	if (!list_empty(&meta->iommu_maps)) {
		WARN(1, "%s: DMA Buffer %pK being destroyed with outstanding iommu mappings!\n",
		     __func__, meta->buffer);
	}
	rb_erase(&meta->node, &iommu_root);
	kfree(meta);
}

static void msm_iommu_meta_put(struct msm_iommu_meta *meta)
{
	/*
	 * Need to lock here to prevent race against map/unmap
	 */
	mutex_lock(&msm_iommu_map_mutex);
	kref_put(&meta->ref, msm_iommu_meta_destroy);
	mutex_unlock(&msm_iommu_map_mutex);
}

static void msm_iommu_map_release(struct kref *kref)
{
	struct msm_iommu_map *map = container_of(kref, struct msm_iommu_map,
						ref);
	struct sg_table table;

	table.nents = table.orig_nents = map->nents;
	table.sgl = map->sgl;
	list_del(&map->lnode);

	/* Skip an additional cache maintenance on the dma unmap path */
	if (!(map->attrs & DMA_ATTR_SKIP_CPU_SYNC))
		map->attrs |= DMA_ATTR_SKIP_CPU_SYNC;
	dma_unmap_sg_attrs(map->dev, map->sgl, map->nents, map->dir,
			map->attrs);
	sg_free_table(&table);
	kfree(map);
}

void msm_dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sgl,
			    int nents, enum dma_data_direction dir,
			    struct dma_buf *dma_buf, unsigned long attrs)
{
	struct msm_iommu_map *iommu_map;
	struct msm_iommu_meta *meta;

	mutex_lock(&msm_iommu_map_mutex);
	meta = msm_iommu_meta_lookup(dma_buf->priv);
	if (!meta) {
		WARN(1, "%s: (%pK) was never mapped\n", __func__, dma_buf);
		mutex_unlock(&msm_iommu_map_mutex);
		goto out;

	}
	mutex_unlock(&msm_iommu_map_mutex);

	mutex_lock(&meta->lock);
	iommu_map = msm_iommu_lookup(meta, dev);

	if (!iommu_map) {
		WARN(1, "%s: (%pK) was never mapped for device  %p\n", __func__,
				dma_buf, dev);
		mutex_unlock(&meta->lock);
		goto out;
	}

	if (dir != iommu_map->dir)
		WARN(1, "%s: (%pK) dir:%d differs from original dir:%d\n",
		     __func__, dma_buf, dir, iommu_map->dir);

	if (attrs && ((attrs & DMA_ATTR_SKIP_CPU_SYNC) == 0))
		dma_sync_sg_for_cpu(dev, iommu_map->sgl, iommu_map->nents, dir);

	iommu_map->attrs = attrs;
	kref_put(&iommu_map->ref, msm_iommu_map_release);
	mutex_unlock(&meta->lock);

	msm_iommu_meta_put(meta);

out:
	return;
}
EXPORT_SYMBOL(msm_dma_unmap_sg_attrs);

int msm_dma_unmap_all_for_dev(struct device *dev)
{
	int ret = 0;
	struct msm_iommu_meta *meta;
	struct rb_root *root;
	struct rb_node *meta_node;

	mutex_lock(&msm_iommu_map_mutex);
	root = &iommu_root;
	meta_node = rb_first(root);
	while (meta_node) {
		struct msm_iommu_map *iommu_map;
		struct msm_iommu_map *iommu_map_next;

		meta = rb_entry(meta_node, struct msm_iommu_meta, node);
		mutex_lock(&meta->lock);
		list_for_each_entry_safe(iommu_map, iommu_map_next,
						&meta->iommu_maps, lnode)
			if (iommu_map->dev == dev)
				if (!kref_put(&iommu_map->ref,
						msm_iommu_map_release))
					ret = -EINVAL;

		mutex_unlock(&meta->lock);
		meta_node = rb_next(meta_node);
	}
	mutex_unlock(&msm_iommu_map_mutex);

	return ret;
}
EXPORT_SYMBOL(msm_dma_unmap_all_for_dev);

/*
 * Only to be called by ION code when a buffer is freed
 */
void msm_dma_buf_freed(void *buffer)
{
	struct msm_iommu_map *iommu_map;
	struct msm_iommu_map *iommu_map_next;
	struct msm_iommu_meta *meta;

	mutex_lock(&msm_iommu_map_mutex);
	meta = msm_iommu_meta_lookup(buffer);
	if (!meta) {
		/* Already unmapped (assuming no late unmapping) */
		mutex_unlock(&msm_iommu_map_mutex);
		return;
	}
	mutex_unlock(&msm_iommu_map_mutex);

	mutex_lock(&meta->lock);

	list_for_each_entry_safe(iommu_map, iommu_map_next, &meta->iommu_maps,
				 lnode)
		kref_put(&iommu_map->ref, msm_iommu_map_release);

	if (!list_empty(&meta->iommu_maps)) {
		WARN(1, "%s: DMA buffer %pK destroyed with outstanding iommu mappings\n",
		     __func__, meta->buffer);
	}

	INIT_LIST_HEAD(&meta->iommu_maps);
	mutex_unlock(&meta->lock);

	msm_iommu_meta_put(meta);
}
EXPORT_SYMBOL(msm_dma_buf_freed);

MODULE_LICENSE("GPL v2");
