// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_query.h"

#include <linux/nospec.h>

#include <drm/ttm/ttm_placement.h>
#include <drm/xe_drm.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_engine.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_guc_hwconfig.h"
#include "xe_macros.h"

static const enum xe_engine_class xe_to_user_engine_class[] = {
	[XE_ENGINE_CLASS_RENDER] = DRM_XE_ENGINE_CLASS_RENDER,
	[XE_ENGINE_CLASS_COPY] = DRM_XE_ENGINE_CLASS_COPY,
	[XE_ENGINE_CLASS_VIDEO_DECODE] = DRM_XE_ENGINE_CLASS_VIDEO_DECODE,
	[XE_ENGINE_CLASS_VIDEO_ENHANCE] = DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE,
	[XE_ENGINE_CLASS_COMPUTE] = DRM_XE_ENGINE_CLASS_COMPUTE,
};

static size_t calc_hw_engine_info_size(struct xe_device *xe)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_gt *gt;
	u8 gt_id;
	int i = 0;

	for_each_gt(gt, xe, gt_id)
		for_each_hw_engine(hwe, gt, id) {
			if (xe_hw_engine_is_reserved(hwe))
				continue;
			i++;
		}

	return i * sizeof(struct drm_xe_engine_class_instance);
}

static int query_engines(struct xe_device *xe,
			 struct drm_xe_device_query *query)
{
	size_t size = calc_hw_engine_info_size(xe);
	struct drm_xe_engine_class_instance __user *query_ptr =
		u64_to_user_ptr(query->data);
	struct drm_xe_engine_class_instance *hw_engine_info;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_gt *gt;
	u8 gt_id;
	int i = 0;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_ERR(xe, query->size != size)) {
		return -EINVAL;
	}

	hw_engine_info = kmalloc(size, GFP_KERNEL);
	if (XE_IOCTL_ERR(xe, !hw_engine_info))
		return -ENOMEM;

	for_each_gt(gt, xe, gt_id)
		for_each_hw_engine(hwe, gt, id) {
			if (xe_hw_engine_is_reserved(hwe))
				continue;

			hw_engine_info[i].engine_class =
				xe_to_user_engine_class[hwe->class];
			hw_engine_info[i].engine_instance =
				hwe->logical_instance;
			hw_engine_info[i++].gt_id = gt->info.id;
		}

	if (copy_to_user(query_ptr, hw_engine_info, size)) {
		kfree(hw_engine_info);
		return -EFAULT;
	}
	kfree(hw_engine_info);

	return 0;
}

static size_t calc_memory_usage_size(struct xe_device *xe)
{
	u32 num_managers = 1;
	int i;

	for (i = XE_PL_VRAM0; i <= XE_PL_VRAM1; ++i)
		if (ttm_manager_type(&xe->ttm, i))
			num_managers++;

	return offsetof(struct drm_xe_query_mem_usage, regions[num_managers]);
}

static int query_memory_usage(struct xe_device *xe,
			      struct drm_xe_device_query *query)
{
	size_t size = calc_memory_usage_size(xe);
	struct drm_xe_query_mem_usage *usage;
	struct drm_xe_query_mem_usage __user *query_ptr =
		u64_to_user_ptr(query->data);
	struct ttm_resource_manager *man;
	int ret, i;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_ERR(xe, query->size != size)) {
		return -EINVAL;
	}

	usage = kzalloc(size, GFP_KERNEL);
	if (XE_IOCTL_ERR(xe, !usage))
		return -ENOMEM;

	man = ttm_manager_type(&xe->ttm, XE_PL_TT);
	usage->regions[0].mem_class = XE_MEM_REGION_CLASS_SYSMEM;
	usage->regions[0].instance = 0;
	usage->regions[0].min_page_size = PAGE_SIZE;
	usage->regions[0].max_page_size = PAGE_SIZE;
	usage->regions[0].total_size = man->size << PAGE_SHIFT;
	usage->regions[0].used = ttm_resource_manager_usage(man);
	usage->num_regions = 1;

	for (i = XE_PL_VRAM0; i <= XE_PL_VRAM1; ++i) {
		man = ttm_manager_type(&xe->ttm, i);
		if (man) {
			usage->regions[usage->num_regions].mem_class =
				XE_MEM_REGION_CLASS_VRAM;
			usage->regions[usage->num_regions].instance =
				usage->num_regions;
			usage->regions[usage->num_regions].min_page_size =
				xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K ?
				SZ_64K : PAGE_SIZE;
			usage->regions[usage->num_regions].max_page_size =
				SZ_1G;
			usage->regions[usage->num_regions].total_size =
				man->size;
			usage->regions[usage->num_regions++].used =
				ttm_resource_manager_usage(man);
		}
	}

	if (!copy_to_user(query_ptr, usage, size))
		ret = 0;
	else
		ret = -ENOSPC;

	kfree(usage);
	return ret;
}

static int query_config(struct xe_device *xe, struct drm_xe_device_query *query)
{
	u32 num_params = XE_QUERY_CONFIG_NUM_PARAM;
	size_t size =
		sizeof(struct drm_xe_query_config) + num_params * sizeof(u64);
	struct drm_xe_query_config __user *query_ptr =
		u64_to_user_ptr(query->data);
	struct drm_xe_query_config *config;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_ERR(xe, query->size != size)) {
		return -EINVAL;
	}

	config = kzalloc(size, GFP_KERNEL);
	if (XE_IOCTL_ERR(xe, !config))
		return -ENOMEM;

	config->num_params = num_params;
	config->info[XE_QUERY_CONFIG_REV_AND_DEVICE_ID] =
		xe->info.devid | (xe->info.revid << 16);
	if (to_gt(xe)->mem.vram.size)
		config->info[XE_QUERY_CONFIG_FLAGS] =
			XE_QUERY_CONFIG_FLAGS_HAS_VRAM;
	if (xe->info.enable_guc)
		config->info[XE_QUERY_CONFIG_FLAGS] |=
			XE_QUERY_CONFIG_FLAGS_USE_GUC;
	config->info[XE_QUERY_CONFIG_MIN_ALIGNEMENT] =
		xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K ? SZ_64K : SZ_4K;
	config->info[XE_QUERY_CONFIG_VA_BITS] = 12 +
		(9 * (xe->info.vm_max_level + 1));
	config->info[XE_QUERY_CONFIG_GT_COUNT] = xe->info.tile_count;
	config->info[XE_QUERY_CONFIG_MEM_REGION_COUNT] =
		hweight_long(xe->info.mem_region_mask);
	config->info[XE_QUERY_CONFIG_MAX_ENGINE_PRIORITY] =
		xe_engine_device_get_max_priority(xe);

	if (copy_to_user(query_ptr, config, size)) {
		kfree(config);
		return -EFAULT;
	}
	kfree(config);

	return 0;
}

static int query_gts(struct xe_device *xe, struct drm_xe_device_query *query)
{
	struct xe_gt *gt;
	size_t size = sizeof(struct drm_xe_query_gts) +
		xe->info.tile_count * sizeof(struct drm_xe_query_gt);
	struct drm_xe_query_gts __user *query_ptr =
		u64_to_user_ptr(query->data);
	struct drm_xe_query_gts *gts;
	u8 id;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_ERR(xe, query->size != size)) {
		return -EINVAL;
	}

	gts = kzalloc(size, GFP_KERNEL);
	if (XE_IOCTL_ERR(xe, !gts))
		return -ENOMEM;

	gts->num_gt = xe->info.tile_count;
	for_each_gt(gt, xe, id) {
		if (id == 0)
			gts->gts[id].type = XE_QUERY_GT_TYPE_MAIN;
		else if (xe_gt_is_media_type(gt))
			gts->gts[id].type = XE_QUERY_GT_TYPE_MEDIA;
		else
			gts->gts[id].type = XE_QUERY_GT_TYPE_REMOTE;
		gts->gts[id].instance = id;
		gts->gts[id].clock_freq = gt->info.clock_freq;
		if (!IS_DGFX(xe))
			gts->gts[id].native_mem_regions = 0x1;
		else
			gts->gts[id].native_mem_regions =
				BIT(gt->info.vram_id) << 1;
		gts->gts[id].slow_mem_regions = xe->info.mem_region_mask ^
			gts->gts[id].native_mem_regions;
	}

	if (copy_to_user(query_ptr, gts, size)) {
		kfree(gts);
		return -EFAULT;
	}
	kfree(gts);

	return 0;
}

static int query_hwconfig(struct xe_device *xe,
			  struct drm_xe_device_query *query)
{
	struct xe_gt *gt = xe_device_get_gt(xe, 0);
	size_t size = xe_guc_hwconfig_size(&gt->uc.guc);
	void __user *query_ptr = u64_to_user_ptr(query->data);
	void *hwconfig;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_ERR(xe, query->size != size)) {
		return -EINVAL;
	}

	hwconfig = kzalloc(size, GFP_KERNEL);
	if (XE_IOCTL_ERR(xe, !hwconfig))
		return -ENOMEM;

	xe_device_mem_access_get(xe);
	xe_guc_hwconfig_copy(&gt->uc.guc, hwconfig);
	xe_device_mem_access_put(xe);

	if (copy_to_user(query_ptr, hwconfig, size)) {
		kfree(hwconfig);
		return -EFAULT;
	}
	kfree(hwconfig);

	return 0;
}

static size_t calc_topo_query_size(struct xe_device *xe)
{
	return xe->info.tile_count *
		(3 * sizeof(struct drm_xe_query_topology_mask) +
		 sizeof_field(struct xe_gt, fuse_topo.g_dss_mask) +
		 sizeof_field(struct xe_gt, fuse_topo.c_dss_mask) +
		 sizeof_field(struct xe_gt, fuse_topo.eu_mask_per_dss));
}

static void __user *copy_mask(void __user *ptr,
			      struct drm_xe_query_topology_mask *topo,
			      void *mask, size_t mask_size)
{
	topo->num_bytes = mask_size;

	if (copy_to_user(ptr, topo, sizeof(*topo)))
		return ERR_PTR(-EFAULT);
	ptr += sizeof(topo);

	if (copy_to_user(ptr, mask, mask_size))
		return ERR_PTR(-EFAULT);
	ptr += mask_size;

	return ptr;
}

static int query_gt_topology(struct xe_device *xe,
			     struct drm_xe_device_query *query)
{
	void __user *query_ptr = u64_to_user_ptr(query->data);
	size_t size = calc_topo_query_size(xe);
	struct drm_xe_query_topology_mask topo;
	struct xe_gt *gt;
	int id;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_ERR(xe, query->size != size)) {
		return -EINVAL;
	}

	for_each_gt(gt, xe, id) {
		topo.gt_id = id;

		topo.type = XE_TOPO_DSS_GEOMETRY;
		query_ptr = copy_mask(query_ptr, &topo,
				      gt->fuse_topo.g_dss_mask,
				      sizeof(gt->fuse_topo.g_dss_mask));
		if (IS_ERR(query_ptr))
			return PTR_ERR(query_ptr);

		topo.type = XE_TOPO_DSS_COMPUTE;
		query_ptr = copy_mask(query_ptr, &topo,
				      gt->fuse_topo.c_dss_mask,
				      sizeof(gt->fuse_topo.c_dss_mask));
		if (IS_ERR(query_ptr))
			return PTR_ERR(query_ptr);

		topo.type = XE_TOPO_EU_PER_DSS;
		query_ptr = copy_mask(query_ptr, &topo,
				      gt->fuse_topo.eu_mask_per_dss,
				      sizeof(gt->fuse_topo.eu_mask_per_dss));
		if (IS_ERR(query_ptr))
			return PTR_ERR(query_ptr);
	}

	return 0;
}

static int (* const xe_query_funcs[])(struct xe_device *xe,
				      struct drm_xe_device_query *query) = {
	query_engines,
	query_memory_usage,
	query_config,
	query_gts,
	query_hwconfig,
	query_gt_topology,
};

int xe_query_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct drm_xe_device_query *query = data;
	u32 idx;

	if (XE_IOCTL_ERR(xe, query->extensions) ||
	    XE_IOCTL_ERR(xe, query->reserved[0] || query->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, query->query > ARRAY_SIZE(xe_query_funcs)))
		return -EINVAL;

	idx = array_index_nospec(query->query, ARRAY_SIZE(xe_query_funcs));
	if (XE_IOCTL_ERR(xe, !xe_query_funcs[idx]))
		return -EINVAL;

	return xe_query_funcs[idx](xe, query);
}
