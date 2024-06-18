// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015-2018 Etnaviv Project
 */

#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

#include "common.xml.h"
#include "etnaviv_cmdbuf.h"
#include "etnaviv_drv.h"
#include "etnaviv_gem.h"
#include "etnaviv_gpu.h"
#include "etnaviv_mmu.h"

static void etnaviv_context_unmap(struct etnaviv_iommu_context *context,
				 unsigned long iova, size_t size)
{
	size_t unmapped_page, unmapped = 0;
	size_t pgsize = SZ_4K;

	if (!IS_ALIGNED(iova | size, pgsize)) {
		pr_err("unaligned: iova 0x%lx size 0x%zx min_pagesz 0x%zx\n",
		       iova, size, pgsize);
		return;
	}

	while (unmapped < size) {
		unmapped_page = context->global->ops->unmap(context, iova,
							    pgsize);
		if (!unmapped_page)
			break;

		iova += unmapped_page;
		unmapped += unmapped_page;
	}
}

static int etnaviv_context_map(struct etnaviv_iommu_context *context,
			      unsigned long iova, phys_addr_t paddr,
			      size_t size, int prot)
{
	unsigned long orig_iova = iova;
	size_t pgsize = SZ_4K;
	size_t orig_size = size;
	int ret = 0;

	if (!IS_ALIGNED(iova | paddr | size, pgsize)) {
		pr_err("unaligned: iova 0x%lx pa %pa size 0x%zx min_pagesz 0x%zx\n",
		       iova, &paddr, size, pgsize);
		return -EINVAL;
	}

	while (size) {
		ret = context->global->ops->map(context, iova, paddr, pgsize,
						prot);
		if (ret)
			break;

		iova += pgsize;
		paddr += pgsize;
		size -= pgsize;
	}

	/* unroll mapping in case something went wrong */
	if (ret)
		etnaviv_context_unmap(context, orig_iova, orig_size - size);

	return ret;
}

static int etnaviv_iommu_map(struct etnaviv_iommu_context *context, u32 iova,
			     struct sg_table *sgt, int prot)
{	struct scatterlist *sg;
	unsigned int da = iova;
	unsigned int i;
	int ret;

	if (!context || !sgt)
		return -EINVAL;

	for_each_sgtable_dma_sg(sgt, sg, i) {
		phys_addr_t pa = sg_dma_address(sg) - sg->offset;
		size_t bytes = sg_dma_len(sg) + sg->offset;

		VERB("map[%d]: %08x %pap(%zx)", i, iova, &pa, bytes);

		ret = etnaviv_context_map(context, da, pa, bytes, prot);
		if (ret)
			goto fail;

		da += bytes;
	}

	context->flush_seq++;

	return 0;

fail:
	etnaviv_context_unmap(context, iova, da - iova);
	return ret;
}

static void etnaviv_iommu_unmap(struct etnaviv_iommu_context *context, u32 iova,
				struct sg_table *sgt, unsigned len)
{
	struct scatterlist *sg;
	unsigned int da = iova;
	int i;

	for_each_sgtable_dma_sg(sgt, sg, i) {
		size_t bytes = sg_dma_len(sg) + sg->offset;

		etnaviv_context_unmap(context, da, bytes);

		VERB("unmap[%d]: %08x(%zx)", i, iova, bytes);

		BUG_ON(!PAGE_ALIGNED(bytes));

		da += bytes;
	}

	context->flush_seq++;
}

static void etnaviv_iommu_remove_mapping(struct etnaviv_iommu_context *context,
	struct etnaviv_vram_mapping *mapping)
{
	struct etnaviv_gem_object *etnaviv_obj = mapping->object;

	lockdep_assert_held(&context->lock);

	etnaviv_iommu_unmap(context, mapping->vram_node.start,
			    etnaviv_obj->sgt, etnaviv_obj->base.size);
	drm_mm_remove_node(&mapping->vram_node);
}

void etnaviv_iommu_reap_mapping(struct etnaviv_vram_mapping *mapping)
{
	struct etnaviv_iommu_context *context = mapping->context;

	lockdep_assert_held(&context->lock);
	WARN_ON(mapping->use);

	etnaviv_iommu_remove_mapping(context, mapping);
	etnaviv_iommu_context_put(mapping->context);
	mapping->context = NULL;
	list_del_init(&mapping->mmu_node);
}

static int etnaviv_iommu_find_iova(struct etnaviv_iommu_context *context,
				   struct drm_mm_node *node, size_t size)
{
	struct etnaviv_vram_mapping *free = NULL;
	enum drm_mm_insert_mode mode = DRM_MM_INSERT_LOW;
	int ret;

	lockdep_assert_held(&context->lock);

	while (1) {
		struct etnaviv_vram_mapping *m, *n;
		struct drm_mm_scan scan;
		struct list_head list;
		bool found;

		ret = drm_mm_insert_node_in_range(&context->mm, node,
						  size, 0, 0, 0, U64_MAX, mode);
		if (ret != -ENOSPC)
			break;

		/* Try to retire some entries */
		drm_mm_scan_init(&scan, &context->mm, size, 0, 0, mode);

		found = 0;
		INIT_LIST_HEAD(&list);
		list_for_each_entry(free, &context->mappings, mmu_node) {
			/* If this vram node has not been used, skip this. */
			if (!free->vram_node.mm)
				continue;

			/*
			 * If the iova is pinned, then it's in-use,
			 * so we must keep its mapping.
			 */
			if (free->use)
				continue;

			list_add(&free->scan_node, &list);
			if (drm_mm_scan_add_block(&scan, &free->vram_node)) {
				found = true;
				break;
			}
		}

		if (!found) {
			/* Nothing found, clean up and fail */
			list_for_each_entry_safe(m, n, &list, scan_node)
				BUG_ON(drm_mm_scan_remove_block(&scan, &m->vram_node));
			break;
		}

		/*
		 * drm_mm does not allow any other operations while
		 * scanning, so we have to remove all blocks first.
		 * If drm_mm_scan_remove_block() returns false, we
		 * can leave the block pinned.
		 */
		list_for_each_entry_safe(m, n, &list, scan_node)
			if (!drm_mm_scan_remove_block(&scan, &m->vram_node))
				list_del_init(&m->scan_node);

		/*
		 * Unmap the blocks which need to be reaped from the MMU.
		 * Clear the mmu pointer to prevent the mapping_get finding
		 * this mapping.
		 */
		list_for_each_entry_safe(m, n, &list, scan_node) {
			etnaviv_iommu_reap_mapping(m);
			list_del_init(&m->scan_node);
		}

		mode = DRM_MM_INSERT_EVICT;

		/*
		 * We removed enough mappings so that the new allocation will
		 * succeed, retry the allocation one more time.
		 */
	}

	return ret;
}

static int etnaviv_iommu_insert_exact(struct etnaviv_iommu_context *context,
		   struct drm_mm_node *node, size_t size, u64 va)
{
	struct etnaviv_vram_mapping *m, *n;
	struct drm_mm_node *scan_node;
	LIST_HEAD(scan_list);
	int ret;

	lockdep_assert_held(&context->lock);

	ret = drm_mm_insert_node_in_range(&context->mm, node, size, 0, 0, va,
					  va + size, DRM_MM_INSERT_LOWEST);
	if (ret != -ENOSPC)
		return ret;

	/*
	 * When we can't insert the node, due to a existing mapping blocking
	 * the address space, there are two possible reasons:
	 * 1. Userspace genuinely messed up and tried to reuse address space
	 * before the last job using this VMA has finished executing.
	 * 2. The existing buffer mappings are idle, but the buffers are not
	 * destroyed yet (likely due to being referenced by another context) in
	 * which case the mappings will not be cleaned up and we must reap them
	 * here to make space for the new mapping.
	 */

	drm_mm_for_each_node_in_range(scan_node, &context->mm, va, va + size) {
		m = container_of(scan_node, struct etnaviv_vram_mapping,
				 vram_node);

		if (m->use)
			return -ENOSPC;

		list_add(&m->scan_node, &scan_list);
	}

	list_for_each_entry_safe(m, n, &scan_list, scan_node) {
		etnaviv_iommu_reap_mapping(m);
		list_del_init(&m->scan_node);
	}

	return drm_mm_insert_node_in_range(&context->mm, node, size, 0, 0, va,
					   va + size, DRM_MM_INSERT_LOWEST);
}

int etnaviv_iommu_map_gem(struct etnaviv_iommu_context *context,
	struct etnaviv_gem_object *etnaviv_obj, u32 memory_base,
	struct etnaviv_vram_mapping *mapping, u64 va)
{
	struct sg_table *sgt = etnaviv_obj->sgt;
	struct drm_mm_node *node;
	int ret;

	lockdep_assert_held(&etnaviv_obj->lock);

	mutex_lock(&context->lock);

	/* v1 MMU can optimize single entry (contiguous) scatterlists */
	if (context->global->version == ETNAVIV_IOMMU_V1 &&
	    sgt->nents == 1 && !(etnaviv_obj->flags & ETNA_BO_FORCE_MMU)) {
		u32 iova;

		iova = sg_dma_address(sgt->sgl) - memory_base;
		if (iova < 0x80000000 - sg_dma_len(sgt->sgl)) {
			mapping->iova = iova;
			mapping->context = etnaviv_iommu_context_get(context);
			list_add_tail(&mapping->mmu_node, &context->mappings);
			ret = 0;
			goto unlock;
		}
	}

	node = &mapping->vram_node;

	if (va)
		ret = etnaviv_iommu_insert_exact(context, node,
						 etnaviv_obj->base.size, va);
	else
		ret = etnaviv_iommu_find_iova(context, node,
					      etnaviv_obj->base.size);
	if (ret < 0)
		goto unlock;

	mapping->iova = node->start;
	ret = etnaviv_iommu_map(context, node->start, sgt,
				ETNAVIV_PROT_READ | ETNAVIV_PROT_WRITE);

	if (ret < 0) {
		drm_mm_remove_node(node);
		goto unlock;
	}

	mapping->context = etnaviv_iommu_context_get(context);
	list_add_tail(&mapping->mmu_node, &context->mappings);
unlock:
	mutex_unlock(&context->lock);

	return ret;
}

void etnaviv_iommu_unmap_gem(struct etnaviv_iommu_context *context,
	struct etnaviv_vram_mapping *mapping)
{
	WARN_ON(mapping->use);

	mutex_lock(&context->lock);

	/* Bail if the mapping has been reaped by another thread */
	if (!mapping->context) {
		mutex_unlock(&context->lock);
		return;
	}

	/* If the vram node is on the mm, unmap and remove the node */
	if (mapping->vram_node.mm == &context->mm)
		etnaviv_iommu_remove_mapping(context, mapping);

	list_del(&mapping->mmu_node);
	mutex_unlock(&context->lock);
	etnaviv_iommu_context_put(context);
}

static void etnaviv_iommu_context_free(struct kref *kref)
{
	struct etnaviv_iommu_context *context =
		container_of(kref, struct etnaviv_iommu_context, refcount);

	etnaviv_cmdbuf_suballoc_unmap(context, &context->cmdbuf_mapping);

	context->global->ops->free(context);
}
void etnaviv_iommu_context_put(struct etnaviv_iommu_context *context)
{
	kref_put(&context->refcount, etnaviv_iommu_context_free);
}

struct etnaviv_iommu_context *
etnaviv_iommu_context_init(struct etnaviv_iommu_global *global,
			   struct etnaviv_cmdbuf_suballoc *suballoc)
{
	struct etnaviv_iommu_context *ctx;
	int ret;

	if (global->version == ETNAVIV_IOMMU_V1)
		ctx = etnaviv_iommuv1_context_alloc(global);
	else
		ctx = etnaviv_iommuv2_context_alloc(global);

	if (!ctx)
		return NULL;

	ret = etnaviv_cmdbuf_suballoc_map(suballoc, ctx, &ctx->cmdbuf_mapping,
					  global->memory_base);
	if (ret)
		goto out_free;

	if (global->version == ETNAVIV_IOMMU_V1 &&
	    ctx->cmdbuf_mapping.iova > 0x80000000) {
		dev_err(global->dev,
		        "command buffer outside valid memory window\n");
		goto out_unmap;
	}

	return ctx;

out_unmap:
	etnaviv_cmdbuf_suballoc_unmap(ctx, &ctx->cmdbuf_mapping);
out_free:
	global->ops->free(ctx);
	return NULL;
}

void etnaviv_iommu_restore(struct etnaviv_gpu *gpu,
			   struct etnaviv_iommu_context *context)
{
	context->global->ops->restore(gpu, context);
}

int etnaviv_iommu_get_suballoc_va(struct etnaviv_iommu_context *context,
				  struct etnaviv_vram_mapping *mapping,
				  u32 memory_base, dma_addr_t paddr,
				  size_t size)
{
	mutex_lock(&context->lock);

	if (mapping->use > 0) {
		mapping->use++;
		mutex_unlock(&context->lock);
		return 0;
	}

	/*
	 * For MMUv1 we don't add the suballoc region to the pagetables, as
	 * those GPUs can only work with cmdbufs accessed through the linear
	 * window. Instead we manufacture a mapping to make it look uniform
	 * to the upper layers.
	 */
	if (context->global->version == ETNAVIV_IOMMU_V1) {
		mapping->iova = paddr - memory_base;
	} else {
		struct drm_mm_node *node = &mapping->vram_node;
		int ret;

		ret = etnaviv_iommu_find_iova(context, node, size);
		if (ret < 0) {
			mutex_unlock(&context->lock);
			return ret;
		}

		mapping->iova = node->start;
		ret = etnaviv_context_map(context, node->start, paddr, size,
					  ETNAVIV_PROT_READ);
		if (ret < 0) {
			drm_mm_remove_node(node);
			mutex_unlock(&context->lock);
			return ret;
		}

		context->flush_seq++;
	}

	list_add_tail(&mapping->mmu_node, &context->mappings);
	mapping->use = 1;

	mutex_unlock(&context->lock);

	return 0;
}

void etnaviv_iommu_put_suballoc_va(struct etnaviv_iommu_context *context,
		  struct etnaviv_vram_mapping *mapping)
{
	struct drm_mm_node *node = &mapping->vram_node;

	mutex_lock(&context->lock);
	mapping->use--;

	if (mapping->use > 0 || context->global->version == ETNAVIV_IOMMU_V1) {
		mutex_unlock(&context->lock);
		return;
	}

	etnaviv_context_unmap(context, node->start, node->size);
	drm_mm_remove_node(node);
	mutex_unlock(&context->lock);
}

size_t etnaviv_iommu_dump_size(struct etnaviv_iommu_context *context)
{
	return context->global->ops->dump_size(context);
}

void etnaviv_iommu_dump(struct etnaviv_iommu_context *context, void *buf)
{
	context->global->ops->dump(context, buf);
}

int etnaviv_iommu_global_init(struct etnaviv_gpu *gpu)
{
	enum etnaviv_iommu_version version = ETNAVIV_IOMMU_V1;
	struct etnaviv_drm_private *priv = gpu->drm->dev_private;
	struct etnaviv_iommu_global *global;
	struct device *dev = gpu->drm->dev;

	if (gpu->identity.minor_features1 & chipMinorFeatures1_MMU_VERSION)
		version = ETNAVIV_IOMMU_V2;

	if (priv->mmu_global) {
		if (priv->mmu_global->version != version) {
			dev_err(gpu->dev,
				"MMU version doesn't match global version\n");
			return -ENXIO;
		}

		priv->mmu_global->use++;
		return 0;
	}

	global = kzalloc(sizeof(*global), GFP_KERNEL);
	if (!global)
		return -ENOMEM;

	global->bad_page_cpu = dma_alloc_wc(dev, SZ_4K, &global->bad_page_dma,
					    GFP_KERNEL);
	if (!global->bad_page_cpu)
		goto free_global;

	memset32(global->bad_page_cpu, 0xdead55aa, SZ_4K / sizeof(u32));

	if (version == ETNAVIV_IOMMU_V2) {
		global->v2.pta_cpu = dma_alloc_wc(dev, ETNAVIV_PTA_SIZE,
					       &global->v2.pta_dma, GFP_KERNEL);
		if (!global->v2.pta_cpu)
			goto free_bad_page;
	}

	global->dev = dev;
	global->version = version;
	global->use = 1;
	mutex_init(&global->lock);

	if (version == ETNAVIV_IOMMU_V1)
		global->ops = &etnaviv_iommuv1_ops;
	else
		global->ops = &etnaviv_iommuv2_ops;

	priv->mmu_global = global;

	return 0;

free_bad_page:
	dma_free_wc(dev, SZ_4K, global->bad_page_cpu, global->bad_page_dma);
free_global:
	kfree(global);

	return -ENOMEM;
}

void etnaviv_iommu_global_fini(struct etnaviv_gpu *gpu)
{
	struct etnaviv_drm_private *priv = gpu->drm->dev_private;
	struct etnaviv_iommu_global *global = priv->mmu_global;

	if (!global)
		return;

	if (--global->use > 0)
		return;

	if (global->v2.pta_cpu)
		dma_free_wc(global->dev, ETNAVIV_PTA_SIZE,
			    global->v2.pta_cpu, global->v2.pta_dma);

	if (global->bad_page_cpu)
		dma_free_wc(global->dev, SZ_4K,
			    global->bad_page_cpu, global->bad_page_dma);

	mutex_destroy(&global->lock);
	kfree(global);

	priv->mmu_global = NULL;
}
