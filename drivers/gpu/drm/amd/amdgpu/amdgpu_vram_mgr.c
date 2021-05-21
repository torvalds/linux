/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

#include <linux/dma-mapping.h>
#include "amdgpu.h"
#include "amdgpu_vm.h"
#include "amdgpu_res_cursor.h"
#include "amdgpu_atomfirmware.h"
#include "atom.h"

struct amdgpu_vram_reservation {
	struct list_head node;
	struct drm_mm_node mm_node;
};

static inline struct amdgpu_vram_mgr *
to_vram_mgr(struct ttm_resource_manager *man)
{
	return container_of(man, struct amdgpu_vram_mgr, manager);
}

static inline struct amdgpu_device *
to_amdgpu_device(struct amdgpu_vram_mgr *mgr)
{
	return container_of(mgr, struct amdgpu_device, mman.vram_mgr);
}

/**
 * DOC: mem_info_vram_total
 *
 * The amdgpu driver provides a sysfs API for reporting current total VRAM
 * available on the device
 * The file mem_info_vram_total is used for this and returns the total
 * amount of VRAM in bytes
 */
static ssize_t amdgpu_mem_info_vram_total_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%llu\n", adev->gmc.real_vram_size);
}

/**
 * DOC: mem_info_vis_vram_total
 *
 * The amdgpu driver provides a sysfs API for reporting current total
 * visible VRAM available on the device
 * The file mem_info_vis_vram_total is used for this and returns the total
 * amount of visible VRAM in bytes
 */
static ssize_t amdgpu_mem_info_vis_vram_total_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	return sysfs_emit(buf, "%llu\n", adev->gmc.visible_vram_size);
}

/**
 * DOC: mem_info_vram_used
 *
 * The amdgpu driver provides a sysfs API for reporting current total VRAM
 * available on the device
 * The file mem_info_vram_used is used for this and returns the total
 * amount of currently used VRAM in bytes
 */
static ssize_t amdgpu_mem_info_vram_used_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct ttm_resource_manager *man;

	man = ttm_manager_type(&adev->mman.bdev, TTM_PL_VRAM);
	return sysfs_emit(buf, "%llu\n", amdgpu_vram_mgr_usage(man));
}

/**
 * DOC: mem_info_vis_vram_used
 *
 * The amdgpu driver provides a sysfs API for reporting current total of
 * used visible VRAM
 * The file mem_info_vis_vram_used is used for this and returns the total
 * amount of currently used visible VRAM in bytes
 */
static ssize_t amdgpu_mem_info_vis_vram_used_show(struct device *dev,
						  struct device_attribute *attr,
						  char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);
	struct ttm_resource_manager *man;

	man = ttm_manager_type(&adev->mman.bdev, TTM_PL_VRAM);
	return sysfs_emit(buf, "%llu\n", amdgpu_vram_mgr_vis_usage(man));
}

/**
 * DOC: mem_info_vram_vendor
 *
 * The amdgpu driver provides a sysfs API for reporting the vendor of the
 * installed VRAM
 * The file mem_info_vram_vendor is used for this and returns the name of the
 * vendor.
 */
static ssize_t amdgpu_mem_info_vram_vendor(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(ddev);

	switch (adev->gmc.vram_vendor) {
	case SAMSUNG:
		return sysfs_emit(buf, "samsung\n");
	case INFINEON:
		return sysfs_emit(buf, "infineon\n");
	case ELPIDA:
		return sysfs_emit(buf, "elpida\n");
	case ETRON:
		return sysfs_emit(buf, "etron\n");
	case NANYA:
		return sysfs_emit(buf, "nanya\n");
	case HYNIX:
		return sysfs_emit(buf, "hynix\n");
	case MOSEL:
		return sysfs_emit(buf, "mosel\n");
	case WINBOND:
		return sysfs_emit(buf, "winbond\n");
	case ESMT:
		return sysfs_emit(buf, "esmt\n");
	case MICRON:
		return sysfs_emit(buf, "micron\n");
	default:
		return sysfs_emit(buf, "unknown\n");
	}
}

static DEVICE_ATTR(mem_info_vram_total, S_IRUGO,
		   amdgpu_mem_info_vram_total_show, NULL);
static DEVICE_ATTR(mem_info_vis_vram_total, S_IRUGO,
		   amdgpu_mem_info_vis_vram_total_show,NULL);
static DEVICE_ATTR(mem_info_vram_used, S_IRUGO,
		   amdgpu_mem_info_vram_used_show, NULL);
static DEVICE_ATTR(mem_info_vis_vram_used, S_IRUGO,
		   amdgpu_mem_info_vis_vram_used_show, NULL);
static DEVICE_ATTR(mem_info_vram_vendor, S_IRUGO,
		   amdgpu_mem_info_vram_vendor, NULL);

static const struct attribute *amdgpu_vram_mgr_attributes[] = {
	&dev_attr_mem_info_vram_total.attr,
	&dev_attr_mem_info_vis_vram_total.attr,
	&dev_attr_mem_info_vram_used.attr,
	&dev_attr_mem_info_vis_vram_used.attr,
	&dev_attr_mem_info_vram_vendor.attr,
	NULL
};

/**
 * amdgpu_vram_mgr_vis_size - Calculate visible node size
 *
 * @adev: amdgpu_device pointer
 * @node: MM node structure
 *
 * Calculate how many bytes of the MM node are inside visible VRAM
 */
static u64 amdgpu_vram_mgr_vis_size(struct amdgpu_device *adev,
				    struct drm_mm_node *node)
{
	uint64_t start = node->start << PAGE_SHIFT;
	uint64_t end = (node->size + node->start) << PAGE_SHIFT;

	if (start >= adev->gmc.visible_vram_size)
		return 0;

	return (end > adev->gmc.visible_vram_size ?
		adev->gmc.visible_vram_size : end) - start;
}

/**
 * amdgpu_vram_mgr_bo_visible_size - CPU visible BO size
 *
 * @bo: &amdgpu_bo buffer object (must be in VRAM)
 *
 * Returns:
 * How much of the given &amdgpu_bo buffer object lies in CPU visible VRAM.
 */
u64 amdgpu_vram_mgr_bo_visible_size(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_resource *mem = &bo->tbo.mem;
	struct drm_mm_node *nodes = mem->mm_node;
	unsigned pages = mem->num_pages;
	u64 usage;

	if (amdgpu_gmc_vram_full_visible(&adev->gmc))
		return amdgpu_bo_size(bo);

	if (mem->start >= adev->gmc.visible_vram_size >> PAGE_SHIFT)
		return 0;

	for (usage = 0; nodes && pages; pages -= nodes->size, nodes++)
		usage += amdgpu_vram_mgr_vis_size(adev, nodes);

	return usage;
}

/* Commit the reservation of VRAM pages */
static void amdgpu_vram_mgr_do_reserve(struct ttm_resource_manager *man)
{
	struct amdgpu_vram_mgr *mgr = to_vram_mgr(man);
	struct amdgpu_device *adev = to_amdgpu_device(mgr);
	struct drm_mm *mm = &mgr->mm;
	struct amdgpu_vram_reservation *rsv, *temp;
	uint64_t vis_usage;

	list_for_each_entry_safe(rsv, temp, &mgr->reservations_pending, node) {
		if (drm_mm_reserve_node(mm, &rsv->mm_node))
			continue;

		dev_dbg(adev->dev, "Reservation 0x%llx - %lld, Succeeded\n",
			rsv->mm_node.start, rsv->mm_node.size);

		vis_usage = amdgpu_vram_mgr_vis_size(adev, &rsv->mm_node);
		atomic64_add(vis_usage, &mgr->vis_usage);
		atomic64_add(rsv->mm_node.size << PAGE_SHIFT, &mgr->usage);
		list_move(&rsv->node, &mgr->reserved_pages);
	}
}

/**
 * amdgpu_vram_mgr_reserve_range - Reserve a range from VRAM
 *
 * @man: TTM memory type manager
 * @start: start address of the range in VRAM
 * @size: size of the range
 *
 * Reserve memory from start addess with the specified size in VRAM
 */
int amdgpu_vram_mgr_reserve_range(struct ttm_resource_manager *man,
				  uint64_t start, uint64_t size)
{
	struct amdgpu_vram_mgr *mgr = to_vram_mgr(man);
	struct amdgpu_vram_reservation *rsv;

	rsv = kzalloc(sizeof(*rsv), GFP_KERNEL);
	if (!rsv)
		return -ENOMEM;

	INIT_LIST_HEAD(&rsv->node);
	rsv->mm_node.start = start >> PAGE_SHIFT;
	rsv->mm_node.size = size >> PAGE_SHIFT;

	spin_lock(&mgr->lock);
	list_add_tail(&mgr->reservations_pending, &rsv->node);
	amdgpu_vram_mgr_do_reserve(man);
	spin_unlock(&mgr->lock);

	return 0;
}

/**
 * amdgpu_vram_mgr_query_page_status - query the reservation status
 *
 * @man: TTM memory type manager
 * @start: start address of a page in VRAM
 *
 * Returns:
 *	-EBUSY: the page is still hold and in pending list
 *	0: the page has been reserved
 *	-ENOENT: the input page is not a reservation
 */
int amdgpu_vram_mgr_query_page_status(struct ttm_resource_manager *man,
				      uint64_t start)
{
	struct amdgpu_vram_mgr *mgr = to_vram_mgr(man);
	struct amdgpu_vram_reservation *rsv;
	int ret;

	spin_lock(&mgr->lock);

	list_for_each_entry(rsv, &mgr->reservations_pending, node) {
		if ((rsv->mm_node.start <= start) &&
		    (start < (rsv->mm_node.start + rsv->mm_node.size))) {
			ret = -EBUSY;
			goto out;
		}
	}

	list_for_each_entry(rsv, &mgr->reserved_pages, node) {
		if ((rsv->mm_node.start <= start) &&
		    (start < (rsv->mm_node.start + rsv->mm_node.size))) {
			ret = 0;
			goto out;
		}
	}

	ret = -ENOENT;
out:
	spin_unlock(&mgr->lock);
	return ret;
}

/**
 * amdgpu_vram_mgr_virt_start - update virtual start address
 *
 * @mem: ttm_resource to update
 * @node: just allocated node
 *
 * Calculate a virtual BO start address to easily check if everything is CPU
 * accessible.
 */
static void amdgpu_vram_mgr_virt_start(struct ttm_resource *mem,
				       struct drm_mm_node *node)
{
	unsigned long start;

	start = node->start + node->size;
	if (start > mem->num_pages)
		start -= mem->num_pages;
	else
		start = 0;
	mem->start = max(mem->start, start);
}

/**
 * amdgpu_vram_mgr_new - allocate new ranges
 *
 * @man: TTM memory type manager
 * @tbo: TTM BO we need this range for
 * @place: placement flags and restrictions
 * @mem: the resulting mem object
 *
 * Allocate VRAM for the given BO.
 */
static int amdgpu_vram_mgr_new(struct ttm_resource_manager *man,
			       struct ttm_buffer_object *tbo,
			       const struct ttm_place *place,
			       struct ttm_resource *mem)
{
	unsigned long lpfn, num_nodes, pages_per_node, pages_left, pages;
	struct amdgpu_vram_mgr *mgr = to_vram_mgr(man);
	struct amdgpu_device *adev = to_amdgpu_device(mgr);
	uint64_t vis_usage = 0, mem_bytes, max_bytes;
	struct drm_mm *mm = &mgr->mm;
	enum drm_mm_insert_mode mode;
	struct drm_mm_node *nodes;
	unsigned i;
	int r;

	lpfn = place->lpfn;
	if (!lpfn)
		lpfn = man->size;

	max_bytes = adev->gmc.mc_vram_size;
	if (tbo->type != ttm_bo_type_kernel)
		max_bytes -= AMDGPU_VM_RESERVED_VRAM;

	/* bail out quickly if there's likely not enough VRAM for this BO */
	mem_bytes = (u64)mem->num_pages << PAGE_SHIFT;
	if (atomic64_add_return(mem_bytes, &mgr->usage) > max_bytes) {
		atomic64_sub(mem_bytes, &mgr->usage);
		return -ENOSPC;
	}

	if (place->flags & TTM_PL_FLAG_CONTIGUOUS) {
		pages_per_node = ~0ul;
		num_nodes = 1;
	} else {
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
		pages_per_node = HPAGE_PMD_NR;
#else
		/* default to 2MB */
		pages_per_node = 2UL << (20UL - PAGE_SHIFT);
#endif
		pages_per_node = max_t(uint32_t, pages_per_node,
				       tbo->page_alignment);
		num_nodes = DIV_ROUND_UP(mem->num_pages, pages_per_node);
	}

	nodes = kvmalloc_array((uint32_t)num_nodes, sizeof(*nodes),
			       GFP_KERNEL | __GFP_ZERO);
	if (!nodes) {
		atomic64_sub(mem_bytes, &mgr->usage);
		return -ENOMEM;
	}

	mode = DRM_MM_INSERT_BEST;
	if (place->flags & TTM_PL_FLAG_TOPDOWN)
		mode = DRM_MM_INSERT_HIGH;

	mem->start = 0;
	pages_left = mem->num_pages;

	/* Limit maximum size to 2GB due to SG table limitations */
	pages = min(pages_left, 2UL << (30 - PAGE_SHIFT));

	i = 0;
	spin_lock(&mgr->lock);
	while (pages_left) {
		uint32_t alignment = tbo->page_alignment;

		if (pages >= pages_per_node)
			alignment = pages_per_node;

		r = drm_mm_insert_node_in_range(mm, &nodes[i], pages, alignment,
						0, place->fpfn, lpfn, mode);
		if (unlikely(r)) {
			if (pages > pages_per_node) {
				if (is_power_of_2(pages))
					pages = pages / 2;
				else
					pages = rounddown_pow_of_two(pages);
				continue;
			}
			goto error;
		}

		vis_usage += amdgpu_vram_mgr_vis_size(adev, &nodes[i]);
		amdgpu_vram_mgr_virt_start(mem, &nodes[i]);
		pages_left -= pages;
		++i;

		if (pages > pages_left)
			pages = pages_left;
	}
	spin_unlock(&mgr->lock);

	if (i == 1)
		mem->placement |= TTM_PL_FLAG_CONTIGUOUS;

	atomic64_add(vis_usage, &mgr->vis_usage);
	mem->mm_node = nodes;
	return 0;

error:
	while (i--)
		drm_mm_remove_node(&nodes[i]);
	spin_unlock(&mgr->lock);
	atomic64_sub(mem->num_pages << PAGE_SHIFT, &mgr->usage);

	kvfree(nodes);
	return r;
}

/**
 * amdgpu_vram_mgr_del - free ranges
 *
 * @man: TTM memory type manager
 * @mem: TTM memory object
 *
 * Free the allocated VRAM again.
 */
static void amdgpu_vram_mgr_del(struct ttm_resource_manager *man,
				struct ttm_resource *mem)
{
	struct amdgpu_vram_mgr *mgr = to_vram_mgr(man);
	struct amdgpu_device *adev = to_amdgpu_device(mgr);
	struct drm_mm_node *nodes = mem->mm_node;
	uint64_t usage = 0, vis_usage = 0;
	unsigned pages = mem->num_pages;

	if (!mem->mm_node)
		return;

	spin_lock(&mgr->lock);
	while (pages) {
		pages -= nodes->size;
		drm_mm_remove_node(nodes);
		usage += nodes->size << PAGE_SHIFT;
		vis_usage += amdgpu_vram_mgr_vis_size(adev, nodes);
		++nodes;
	}
	amdgpu_vram_mgr_do_reserve(man);
	spin_unlock(&mgr->lock);

	atomic64_sub(usage, &mgr->usage);
	atomic64_sub(vis_usage, &mgr->vis_usage);

	kvfree(mem->mm_node);
	mem->mm_node = NULL;
}

/**
 * amdgpu_vram_mgr_alloc_sgt - allocate and fill a sg table
 *
 * @adev: amdgpu device pointer
 * @mem: TTM memory object
 * @offset: byte offset from the base of VRAM BO
 * @length: number of bytes to export in sg_table
 * @dev: the other device
 * @dir: dma direction
 * @sgt: resulting sg table
 *
 * Allocate and fill a sg table from a VRAM allocation.
 */
int amdgpu_vram_mgr_alloc_sgt(struct amdgpu_device *adev,
			      struct ttm_resource *mem,
			      u64 offset, u64 length,
			      struct device *dev,
			      enum dma_data_direction dir,
			      struct sg_table **sgt)
{
	struct amdgpu_res_cursor cursor;
	struct scatterlist *sg;
	int num_entries = 0;
	int i, r;

	*sgt = kmalloc(sizeof(**sgt), GFP_KERNEL);
	if (!*sgt)
		return -ENOMEM;

	/* Determine the number of DRM_MM nodes to export */
	amdgpu_res_first(mem, offset, length, &cursor);
	while (cursor.remaining) {
		num_entries++;
		amdgpu_res_next(&cursor, cursor.size);
	}

	r = sg_alloc_table(*sgt, num_entries, GFP_KERNEL);
	if (r)
		goto error_free;

	/* Initialize scatterlist nodes of sg_table */
	for_each_sgtable_sg((*sgt), sg, i)
		sg->length = 0;

	/*
	 * Walk down DRM_MM nodes to populate scatterlist nodes
	 * @note: Use iterator api to get first the DRM_MM node
	 * and the number of bytes from it. Access the following
	 * DRM_MM node(s) if more buffer needs to exported
	 */
	amdgpu_res_first(mem, offset, length, &cursor);
	for_each_sgtable_sg((*sgt), sg, i) {
		phys_addr_t phys = cursor.start + adev->gmc.aper_base;
		size_t size = cursor.size;
		dma_addr_t addr;

		addr = dma_map_resource(dev, phys, size, dir,
					DMA_ATTR_SKIP_CPU_SYNC);
		r = dma_mapping_error(dev, addr);
		if (r)
			goto error_unmap;

		sg_set_page(sg, NULL, size, 0);
		sg_dma_address(sg) = addr;
		sg_dma_len(sg) = size;

		amdgpu_res_next(&cursor, cursor.size);
	}

	return 0;

error_unmap:
	for_each_sgtable_sg((*sgt), sg, i) {
		if (!sg->length)
			continue;

		dma_unmap_resource(dev, sg->dma_address,
				   sg->length, dir,
				   DMA_ATTR_SKIP_CPU_SYNC);
	}
	sg_free_table(*sgt);

error_free:
	kfree(*sgt);
	return r;
}

/**
 * amdgpu_vram_mgr_free_sgt - allocate and fill a sg table
 *
 * @dev: device pointer
 * @dir: data direction of resource to unmap
 * @sgt: sg table to free
 *
 * Free a previously allocate sg table.
 */
void amdgpu_vram_mgr_free_sgt(struct device *dev,
			      enum dma_data_direction dir,
			      struct sg_table *sgt)
{
	struct scatterlist *sg;
	int i;

	for_each_sgtable_sg(sgt, sg, i)
		dma_unmap_resource(dev, sg->dma_address,
				   sg->length, dir,
				   DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(sgt);
	kfree(sgt);
}

/**
 * amdgpu_vram_mgr_usage - how many bytes are used in this domain
 *
 * @man: TTM memory type manager
 *
 * Returns how many bytes are used in this domain.
 */
uint64_t amdgpu_vram_mgr_usage(struct ttm_resource_manager *man)
{
	struct amdgpu_vram_mgr *mgr = to_vram_mgr(man);

	return atomic64_read(&mgr->usage);
}

/**
 * amdgpu_vram_mgr_vis_usage - how many bytes are used in the visible part
 *
 * @man: TTM memory type manager
 *
 * Returns how many bytes are used in the visible part of VRAM
 */
uint64_t amdgpu_vram_mgr_vis_usage(struct ttm_resource_manager *man)
{
	struct amdgpu_vram_mgr *mgr = to_vram_mgr(man);

	return atomic64_read(&mgr->vis_usage);
}

/**
 * amdgpu_vram_mgr_debug - dump VRAM table
 *
 * @man: TTM memory type manager
 * @printer: DRM printer to use
 *
 * Dump the table content using printk.
 */
static void amdgpu_vram_mgr_debug(struct ttm_resource_manager *man,
				  struct drm_printer *printer)
{
	struct amdgpu_vram_mgr *mgr = to_vram_mgr(man);

	spin_lock(&mgr->lock);
	drm_mm_print(&mgr->mm, printer);
	spin_unlock(&mgr->lock);

	drm_printf(printer, "man size:%llu pages, ram usage:%lluMB, vis usage:%lluMB\n",
		   man->size, amdgpu_vram_mgr_usage(man) >> 20,
		   amdgpu_vram_mgr_vis_usage(man) >> 20);
}

static const struct ttm_resource_manager_func amdgpu_vram_mgr_func = {
	.alloc	= amdgpu_vram_mgr_new,
	.free	= amdgpu_vram_mgr_del,
	.debug	= amdgpu_vram_mgr_debug
};

/**
 * amdgpu_vram_mgr_init - init VRAM manager and DRM MM
 *
 * @adev: amdgpu_device pointer
 *
 * Allocate and initialize the VRAM manager.
 */
int amdgpu_vram_mgr_init(struct amdgpu_device *adev)
{
	struct amdgpu_vram_mgr *mgr = &adev->mman.vram_mgr;
	struct ttm_resource_manager *man = &mgr->manager;
	int ret;

	ttm_resource_manager_init(man, adev->gmc.real_vram_size >> PAGE_SHIFT);

	man->func = &amdgpu_vram_mgr_func;

	drm_mm_init(&mgr->mm, 0, man->size);
	spin_lock_init(&mgr->lock);
	INIT_LIST_HEAD(&mgr->reservations_pending);
	INIT_LIST_HEAD(&mgr->reserved_pages);

	/* Add the two VRAM-related sysfs files */
	ret = sysfs_create_files(&adev->dev->kobj, amdgpu_vram_mgr_attributes);
	if (ret)
		DRM_ERROR("Failed to register sysfs\n");

	ttm_set_driver_manager(&adev->mman.bdev, TTM_PL_VRAM, &mgr->manager);
	ttm_resource_manager_set_used(man, true);
	return 0;
}

/**
 * amdgpu_vram_mgr_fini - free and destroy VRAM manager
 *
 * @adev: amdgpu_device pointer
 *
 * Destroy and free the VRAM manager, returns -EBUSY if ranges are still
 * allocated inside it.
 */
void amdgpu_vram_mgr_fini(struct amdgpu_device *adev)
{
	struct amdgpu_vram_mgr *mgr = &adev->mman.vram_mgr;
	struct ttm_resource_manager *man = &mgr->manager;
	int ret;
	struct amdgpu_vram_reservation *rsv, *temp;

	ttm_resource_manager_set_used(man, false);

	ret = ttm_resource_manager_evict_all(&adev->mman.bdev, man);
	if (ret)
		return;

	spin_lock(&mgr->lock);
	list_for_each_entry_safe(rsv, temp, &mgr->reservations_pending, node)
		kfree(rsv);

	list_for_each_entry_safe(rsv, temp, &mgr->reserved_pages, node) {
		drm_mm_remove_node(&rsv->mm_node);
		kfree(rsv);
	}
	drm_mm_takedown(&mgr->mm);
	spin_unlock(&mgr->lock);

	sysfs_remove_files(&adev->dev->kobj, amdgpu_vram_mgr_attributes);

	ttm_resource_manager_cleanup(man);
	ttm_set_driver_manager(&adev->mman.bdev, TTM_PL_VRAM, NULL);
}
