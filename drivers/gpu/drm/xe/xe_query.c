// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_query.h"

#include <linux/nospec.h>
#include <linux/sched/clock.h>

#include <drm/ttm/ttm_placement.h>
#include <generated/xe_wa_oob.h>
#include <uapi/drm/xe_drm.h>

#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_force_wake.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_guc_hwconfig.h"
#include "xe_macros.h"
#include "xe_mmio.h"
#include "xe_oa.h"
#include "xe_ttm_vram_mgr.h"
#include "xe_wa.h"

static const u16 xe_to_user_engine_class[] = {
	[XE_ENGINE_CLASS_RENDER] = DRM_XE_ENGINE_CLASS_RENDER,
	[XE_ENGINE_CLASS_COPY] = DRM_XE_ENGINE_CLASS_COPY,
	[XE_ENGINE_CLASS_VIDEO_DECODE] = DRM_XE_ENGINE_CLASS_VIDEO_DECODE,
	[XE_ENGINE_CLASS_VIDEO_ENHANCE] = DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE,
	[XE_ENGINE_CLASS_COMPUTE] = DRM_XE_ENGINE_CLASS_COMPUTE,
};

static const enum xe_engine_class user_to_xe_engine_class[] = {
	[DRM_XE_ENGINE_CLASS_RENDER] = XE_ENGINE_CLASS_RENDER,
	[DRM_XE_ENGINE_CLASS_COPY] = XE_ENGINE_CLASS_COPY,
	[DRM_XE_ENGINE_CLASS_VIDEO_DECODE] = XE_ENGINE_CLASS_VIDEO_DECODE,
	[DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE] = XE_ENGINE_CLASS_VIDEO_ENHANCE,
	[DRM_XE_ENGINE_CLASS_COMPUTE] = XE_ENGINE_CLASS_COMPUTE,
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

	return sizeof(struct drm_xe_query_engines) +
		i * sizeof(struct drm_xe_engine);
}

typedef u64 (*__ktime_func_t)(void);
static __ktime_func_t __clock_id_to_func(clockid_t clk_id)
{
	/*
	 * Use logic same as the perf subsystem to allow user to select the
	 * reference clock id to be used for timestamps.
	 */
	switch (clk_id) {
	case CLOCK_MONOTONIC:
		return &ktime_get_ns;
	case CLOCK_MONOTONIC_RAW:
		return &ktime_get_raw_ns;
	case CLOCK_REALTIME:
		return &ktime_get_real_ns;
	case CLOCK_BOOTTIME:
		return &ktime_get_boottime_ns;
	case CLOCK_TAI:
		return &ktime_get_clocktai_ns;
	default:
		return NULL;
	}
}

static void
hwe_read_timestamp(struct xe_hw_engine *hwe, u64 *engine_ts, u64 *cpu_ts,
		   u64 *cpu_delta, __ktime_func_t cpu_clock)
{
	struct xe_mmio *mmio = &hwe->gt->mmio;
	u32 upper, lower, old_upper, loop = 0;
	struct xe_reg upper_reg = RING_TIMESTAMP_UDW(hwe->mmio_base),
		      lower_reg = RING_TIMESTAMP(hwe->mmio_base);

	upper = xe_mmio_read32(mmio, upper_reg);
	do {
		*cpu_delta = local_clock();
		*cpu_ts = cpu_clock();
		lower = xe_mmio_read32(mmio, lower_reg);
		*cpu_delta = local_clock() - *cpu_delta;
		old_upper = upper;
		upper = xe_mmio_read32(mmio, upper_reg);
	} while (upper != old_upper && loop++ < 2);

	*engine_ts = (u64)upper << 32 | lower;
}

static int
query_engine_cycles(struct xe_device *xe,
		    struct drm_xe_device_query *query)
{
	struct drm_xe_query_engine_cycles __user *query_ptr;
	struct drm_xe_engine_class_instance *eci;
	struct drm_xe_query_engine_cycles resp;
	size_t size = sizeof(resp);
	__ktime_func_t cpu_clock;
	struct xe_hw_engine *hwe;
	struct xe_gt *gt;
	unsigned int fw_ref;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	query_ptr = u64_to_user_ptr(query->data);
	if (copy_from_user(&resp, query_ptr, size))
		return -EFAULT;

	cpu_clock = __clock_id_to_func(resp.clockid);
	if (!cpu_clock)
		return -EINVAL;

	eci = &resp.eci;
	if (eci->gt_id >= XE_MAX_GT_PER_TILE)
		return -EINVAL;

	gt = xe_device_get_gt(xe, eci->gt_id);
	if (!gt)
		return -EINVAL;

	if (eci->engine_class >= ARRAY_SIZE(user_to_xe_engine_class))
		return -EINVAL;

	hwe = xe_gt_hw_engine(gt, user_to_xe_engine_class[eci->engine_class],
			      eci->engine_instance, true);
	if (!hwe)
		return -EINVAL;

	fw_ref = xe_force_wake_get(gt_to_fw(gt), XE_FORCEWAKE_ALL);
	if (!xe_force_wake_ref_has_domain(fw_ref, XE_FORCEWAKE_ALL))  {
		xe_force_wake_put(gt_to_fw(gt), fw_ref);
		return -EIO;
	}

	hwe_read_timestamp(hwe, &resp.engine_cycles, &resp.cpu_timestamp,
			   &resp.cpu_delta, cpu_clock);

	xe_force_wake_put(gt_to_fw(gt), fw_ref);

	if (GRAPHICS_VER(xe) >= 20)
		resp.width = 64;
	else
		resp.width = 36;

	/* Only write to the output fields of user query */
	if (put_user(resp.cpu_timestamp, &query_ptr->cpu_timestamp) ||
	    put_user(resp.cpu_delta, &query_ptr->cpu_delta) ||
	    put_user(resp.engine_cycles, &query_ptr->engine_cycles) ||
	    put_user(resp.width, &query_ptr->width))
		return -EFAULT;

	return 0;
}

static int query_engines(struct xe_device *xe,
			 struct drm_xe_device_query *query)
{
	size_t size = calc_hw_engine_info_size(xe);
	struct drm_xe_query_engines __user *query_ptr =
		u64_to_user_ptr(query->data);
	struct drm_xe_query_engines *engines;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	struct xe_gt *gt;
	u8 gt_id;
	int i = 0;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	engines = kzalloc(size, GFP_KERNEL);
	if (!engines)
		return -ENOMEM;

	for_each_gt(gt, xe, gt_id)
		for_each_hw_engine(hwe, gt, id) {
			if (xe_hw_engine_is_reserved(hwe))
				continue;

			engines->engines[i].instance.engine_class =
				xe_to_user_engine_class[hwe->class];
			engines->engines[i].instance.engine_instance =
				hwe->logical_instance;
			engines->engines[i].instance.gt_id = gt->info.id;

			i++;
		}

	engines->num_engines = i;

	if (copy_to_user(query_ptr, engines, size)) {
		kfree(engines);
		return -EFAULT;
	}
	kfree(engines);

	return 0;
}

static size_t calc_mem_regions_size(struct xe_device *xe)
{
	u32 num_managers = 1;
	int i;

	for (i = XE_PL_VRAM0; i <= XE_PL_VRAM1; ++i)
		if (ttm_manager_type(&xe->ttm, i))
			num_managers++;

	return offsetof(struct drm_xe_query_mem_regions, mem_regions[num_managers]);
}

static int query_mem_regions(struct xe_device *xe,
			    struct drm_xe_device_query *query)
{
	size_t size = calc_mem_regions_size(xe);
	struct drm_xe_query_mem_regions *mem_regions;
	struct drm_xe_query_mem_regions __user *query_ptr =
		u64_to_user_ptr(query->data);
	struct ttm_resource_manager *man;
	int ret, i;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	mem_regions = kzalloc(size, GFP_KERNEL);
	if (XE_IOCTL_DBG(xe, !mem_regions))
		return -ENOMEM;

	man = ttm_manager_type(&xe->ttm, XE_PL_TT);
	mem_regions->mem_regions[0].mem_class = DRM_XE_MEM_REGION_CLASS_SYSMEM;
	/*
	 * The instance needs to be a unique number that represents the index
	 * in the placement mask used at xe_gem_create_ioctl() for the
	 * xe_bo_create() placement.
	 */
	mem_regions->mem_regions[0].instance = 0;
	mem_regions->mem_regions[0].min_page_size = PAGE_SIZE;
	mem_regions->mem_regions[0].total_size = man->size << PAGE_SHIFT;
	if (perfmon_capable())
		mem_regions->mem_regions[0].used = ttm_resource_manager_usage(man);
	mem_regions->num_mem_regions = 1;

	for (i = XE_PL_VRAM0; i <= XE_PL_VRAM1; ++i) {
		man = ttm_manager_type(&xe->ttm, i);
		if (man) {
			mem_regions->mem_regions[mem_regions->num_mem_regions].mem_class =
				DRM_XE_MEM_REGION_CLASS_VRAM;
			mem_regions->mem_regions[mem_regions->num_mem_regions].instance =
				mem_regions->num_mem_regions;
			mem_regions->mem_regions[mem_regions->num_mem_regions].min_page_size =
				xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K ?
				SZ_64K : PAGE_SIZE;
			mem_regions->mem_regions[mem_regions->num_mem_regions].total_size =
				man->size;

			if (perfmon_capable()) {
				xe_ttm_vram_get_used(man,
					&mem_regions->mem_regions
					[mem_regions->num_mem_regions].used,
					&mem_regions->mem_regions
					[mem_regions->num_mem_regions].cpu_visible_used);
			}

			mem_regions->mem_regions[mem_regions->num_mem_regions].cpu_visible_size =
				xe_ttm_vram_get_cpu_visible_size(man);
			mem_regions->num_mem_regions++;
		}
	}

	if (!copy_to_user(query_ptr, mem_regions, size))
		ret = 0;
	else
		ret = -ENOSPC;

	kfree(mem_regions);
	return ret;
}

static int query_config(struct xe_device *xe, struct drm_xe_device_query *query)
{
	const u32 num_params = DRM_XE_QUERY_CONFIG_MAX_EXEC_QUEUE_PRIORITY + 1;
	size_t size =
		sizeof(struct drm_xe_query_config) + num_params * sizeof(u64);
	struct drm_xe_query_config __user *query_ptr =
		u64_to_user_ptr(query->data);
	struct drm_xe_query_config *config;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	config = kzalloc(size, GFP_KERNEL);
	if (!config)
		return -ENOMEM;

	config->num_params = num_params;
	config->info[DRM_XE_QUERY_CONFIG_REV_AND_DEVICE_ID] =
		xe->info.devid | (xe->info.revid << 16);
	if (xe_device_get_root_tile(xe)->mem.vram.usable_size)
		config->info[DRM_XE_QUERY_CONFIG_FLAGS] =
			DRM_XE_QUERY_CONFIG_FLAG_HAS_VRAM;
	config->info[DRM_XE_QUERY_CONFIG_MIN_ALIGNMENT] =
		xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K ? SZ_64K : SZ_4K;
	config->info[DRM_XE_QUERY_CONFIG_VA_BITS] = xe->info.va_bits;
	config->info[DRM_XE_QUERY_CONFIG_MAX_EXEC_QUEUE_PRIORITY] =
		xe_exec_queue_device_get_max_priority(xe);

	if (copy_to_user(query_ptr, config, size)) {
		kfree(config);
		return -EFAULT;
	}
	kfree(config);

	return 0;
}

static int query_gt_list(struct xe_device *xe, struct drm_xe_device_query *query)
{
	struct xe_gt *gt;
	size_t size = sizeof(struct drm_xe_query_gt_list) +
		xe->info.gt_count * sizeof(struct drm_xe_gt);
	struct drm_xe_query_gt_list __user *query_ptr =
		u64_to_user_ptr(query->data);
	struct drm_xe_query_gt_list *gt_list;
	u8 id;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	gt_list = kzalloc(size, GFP_KERNEL);
	if (!gt_list)
		return -ENOMEM;

	gt_list->num_gt = xe->info.gt_count;

	for_each_gt(gt, xe, id) {
		if (xe_gt_is_media_type(gt))
			gt_list->gt_list[id].type = DRM_XE_QUERY_GT_TYPE_MEDIA;
		else
			gt_list->gt_list[id].type = DRM_XE_QUERY_GT_TYPE_MAIN;
		gt_list->gt_list[id].tile_id = gt_to_tile(gt)->id;
		gt_list->gt_list[id].gt_id = gt->info.id;
		gt_list->gt_list[id].reference_clock = gt->info.reference_clock;
		/*
		 * The mem_regions indexes in the mask below need to
		 * directly identify the struct
		 * drm_xe_query_mem_regions' instance constructed at
		 * query_mem_regions()
		 *
		 * For our current platforms:
		 * Bit 0 -> System Memory
		 * Bit 1 -> VRAM0 on Tile0
		 * Bit 2 -> VRAM1 on Tile1
		 * However the uAPI is generic and it's userspace's
		 * responsibility to check the mem_class, without any
		 * assumption.
		 */
		if (!IS_DGFX(xe))
			gt_list->gt_list[id].near_mem_regions = 0x1;
		else
			gt_list->gt_list[id].near_mem_regions =
				BIT(gt_to_tile(gt)->id) << 1;
		gt_list->gt_list[id].far_mem_regions = xe->info.mem_region_mask ^
			gt_list->gt_list[id].near_mem_regions;

		gt_list->gt_list[id].ip_ver_major =
			REG_FIELD_GET(GMD_ID_ARCH_MASK, gt->info.gmdid);
		gt_list->gt_list[id].ip_ver_minor =
			REG_FIELD_GET(GMD_ID_RELEASE_MASK, gt->info.gmdid);
		gt_list->gt_list[id].ip_ver_rev =
			REG_FIELD_GET(GMD_ID_REVID, gt->info.gmdid);
	}

	if (copy_to_user(query_ptr, gt_list, size)) {
		kfree(gt_list);
		return -EFAULT;
	}
	kfree(gt_list);

	return 0;
}

static int query_hwconfig(struct xe_device *xe,
			  struct drm_xe_device_query *query)
{
	struct xe_gt *gt = xe_root_mmio_gt(xe);
	size_t size = xe_guc_hwconfig_size(&gt->uc.guc);
	void __user *query_ptr = u64_to_user_ptr(query->data);
	void *hwconfig;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	hwconfig = kzalloc(size, GFP_KERNEL);
	if (!hwconfig)
		return -ENOMEM;

	xe_guc_hwconfig_copy(&gt->uc.guc, hwconfig);

	if (copy_to_user(query_ptr, hwconfig, size)) {
		kfree(hwconfig);
		return -EFAULT;
	}
	kfree(hwconfig);

	return 0;
}

static size_t calc_topo_query_size(struct xe_device *xe)
{
	struct xe_gt *gt;
	size_t query_size = 0;
	int id;

	for_each_gt(gt, xe, id) {
		query_size += 3 * sizeof(struct drm_xe_query_topology_mask) +
			sizeof_field(struct xe_gt, fuse_topo.g_dss_mask) +
			sizeof_field(struct xe_gt, fuse_topo.c_dss_mask) +
			sizeof_field(struct xe_gt, fuse_topo.eu_mask_per_dss);

		/* L3bank mask may not be available for some GTs */
		if (!XE_WA(gt, no_media_l3))
			query_size += sizeof(struct drm_xe_query_topology_mask) +
				sizeof_field(struct xe_gt, fuse_topo.l3_bank_mask);
	}

	return query_size;
}

static int copy_mask(void __user **ptr,
		     struct drm_xe_query_topology_mask *topo,
		     void *mask, size_t mask_size)
{
	topo->num_bytes = mask_size;

	if (copy_to_user(*ptr, topo, sizeof(*topo)))
		return -EFAULT;
	*ptr += sizeof(topo);

	if (copy_to_user(*ptr, mask, mask_size))
		return -EFAULT;
	*ptr += mask_size;

	return 0;
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
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	for_each_gt(gt, xe, id) {
		int err;

		topo.gt_id = id;

		topo.type = DRM_XE_TOPO_DSS_GEOMETRY;
		err = copy_mask(&query_ptr, &topo, gt->fuse_topo.g_dss_mask,
				sizeof(gt->fuse_topo.g_dss_mask));
		if (err)
			return err;

		topo.type = DRM_XE_TOPO_DSS_COMPUTE;
		err = copy_mask(&query_ptr, &topo, gt->fuse_topo.c_dss_mask,
				sizeof(gt->fuse_topo.c_dss_mask));
		if (err)
			return err;

		/*
		 * If the kernel doesn't have a way to obtain a correct L3bank
		 * mask, then it's better to omit L3 from the query rather than
		 * reporting bogus or zeroed information to userspace.
		 */
		if (!XE_WA(gt, no_media_l3)) {
			topo.type = DRM_XE_TOPO_L3_BANK;
			err = copy_mask(&query_ptr, &topo, gt->fuse_topo.l3_bank_mask,
					sizeof(gt->fuse_topo.l3_bank_mask));
			if (err)
				return err;
		}

		topo.type = gt->fuse_topo.eu_type == XE_GT_EU_TYPE_SIMD16 ?
			DRM_XE_TOPO_SIMD16_EU_PER_DSS :
			DRM_XE_TOPO_EU_PER_DSS;
		err = copy_mask(&query_ptr, &topo,
				gt->fuse_topo.eu_mask_per_dss,
				sizeof(gt->fuse_topo.eu_mask_per_dss));
		if (err)
			return err;
	}

	return 0;
}

static int
query_uc_fw_version(struct xe_device *xe, struct drm_xe_device_query *query)
{
	struct drm_xe_query_uc_fw_version __user *query_ptr = u64_to_user_ptr(query->data);
	size_t size = sizeof(struct drm_xe_query_uc_fw_version);
	struct drm_xe_query_uc_fw_version resp;
	struct xe_uc_fw_version *version = NULL;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	if (copy_from_user(&resp, query_ptr, size))
		return -EFAULT;

	if (XE_IOCTL_DBG(xe, resp.pad || resp.pad2 || resp.reserved))
		return -EINVAL;

	switch (resp.uc_type) {
	case XE_QUERY_UC_TYPE_GUC_SUBMISSION: {
		struct xe_guc *guc = &xe->tiles[0].primary_gt->uc.guc;

		version = &guc->fw.versions.found[XE_UC_FW_VER_COMPATIBILITY];
		break;
	}
	case XE_QUERY_UC_TYPE_HUC: {
		struct xe_gt *media_gt = NULL;
		struct xe_huc *huc;

		if (MEDIA_VER(xe) >= 13) {
			struct xe_tile *tile;
			u8 gt_id;

			for_each_tile(tile, xe, gt_id) {
				if (tile->media_gt) {
					media_gt = tile->media_gt;
					break;
				}
			}
		} else {
			media_gt = xe->tiles[0].primary_gt;
		}

		if (!media_gt)
			break;

		huc = &media_gt->uc.huc;
		if (huc->fw.status == XE_UC_FIRMWARE_RUNNING)
			version = &huc->fw.versions.found[XE_UC_FW_VER_RELEASE];
		break;
	}
	default:
		return -EINVAL;
	}

	if (version) {
		resp.branch_ver = 0;
		resp.major_ver = version->major;
		resp.minor_ver = version->minor;
		resp.patch_ver = version->patch;
	} else {
		return -ENODEV;
	}

	if (copy_to_user(query_ptr, &resp, size))
		return -EFAULT;

	return 0;
}

static size_t calc_oa_unit_query_size(struct xe_device *xe)
{
	size_t size = sizeof(struct drm_xe_query_oa_units);
	struct xe_gt *gt;
	int i, id;

	for_each_gt(gt, xe, id) {
		for (i = 0; i < gt->oa.num_oa_units; i++) {
			size += sizeof(struct drm_xe_oa_unit);
			size += gt->oa.oa_unit[i].num_engines *
				sizeof(struct drm_xe_engine_class_instance);
		}
	}

	return size;
}

static int query_oa_units(struct xe_device *xe,
			  struct drm_xe_device_query *query)
{
	void __user *query_ptr = u64_to_user_ptr(query->data);
	size_t size = calc_oa_unit_query_size(xe);
	struct drm_xe_query_oa_units *qoa;
	enum xe_hw_engine_id hwe_id;
	struct drm_xe_oa_unit *du;
	struct xe_hw_engine *hwe;
	struct xe_oa_unit *u;
	int gt_id, i, j, ret;
	struct xe_gt *gt;
	u8 *pdu;

	if (query->size == 0) {
		query->size = size;
		return 0;
	} else if (XE_IOCTL_DBG(xe, query->size != size)) {
		return -EINVAL;
	}

	qoa = kzalloc(size, GFP_KERNEL);
	if (!qoa)
		return -ENOMEM;

	pdu = (u8 *)&qoa->oa_units[0];
	for_each_gt(gt, xe, gt_id) {
		for (i = 0; i < gt->oa.num_oa_units; i++) {
			u = &gt->oa.oa_unit[i];
			du = (struct drm_xe_oa_unit *)pdu;

			du->oa_unit_id = u->oa_unit_id;
			du->oa_unit_type = u->type;
			du->oa_timestamp_freq = xe_oa_timestamp_frequency(gt);
			du->capabilities = DRM_XE_OA_CAPS_BASE | DRM_XE_OA_CAPS_SYNCS |
					   DRM_XE_OA_CAPS_OA_BUFFER_SIZE;

			j = 0;
			for_each_hw_engine(hwe, gt, hwe_id) {
				if (!xe_hw_engine_is_reserved(hwe) &&
				    xe_oa_unit_id(hwe) == u->oa_unit_id) {
					du->eci[j].engine_class =
						xe_to_user_engine_class[hwe->class];
					du->eci[j].engine_instance = hwe->logical_instance;
					du->eci[j].gt_id = gt->info.id;
					j++;
				}
			}
			du->num_engines = j;
			pdu += sizeof(*du) + j * sizeof(du->eci[0]);
			qoa->num_oa_units++;
		}
	}

	ret = copy_to_user(query_ptr, qoa, size);
	kfree(qoa);

	return ret ? -EFAULT : 0;
}

static int (* const xe_query_funcs[])(struct xe_device *xe,
				      struct drm_xe_device_query *query) = {
	query_engines,
	query_mem_regions,
	query_config,
	query_gt_list,
	query_hwconfig,
	query_gt_topology,
	query_engine_cycles,
	query_uc_fw_version,
	query_oa_units,
};

int xe_query_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct drm_xe_device_query *query = data;
	u32 idx;

	if (XE_IOCTL_DBG(xe, query->extensions) ||
	    XE_IOCTL_DBG(xe, query->reserved[0] || query->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, query->query >= ARRAY_SIZE(xe_query_funcs)))
		return -EINVAL;

	idx = array_index_nospec(query->query, ARRAY_SIZE(xe_query_funcs));
	if (XE_IOCTL_DBG(xe, !xe_query_funcs[idx]))
		return -EINVAL;

	return xe_query_funcs[idx](xe, query);
}
