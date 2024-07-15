// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_guc_ads.h"

#include <drm/drm_managed.h>

#include "regs/xe_engine_regs.h"
#include "regs/xe_gt_regs.h"
#include "regs/xe_guc_regs.h"
#include "xe_bo.h"
#include "xe_gt.h"
#include "xe_gt_ccs_mode.h"
#include "xe_guc.h"
#include "xe_hw_engine.h"
#include "xe_lrc.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_platform_types.h"

/* Slack of a few additional entries per engine */
#define ADS_REGSET_EXTRA_MAX	8

static struct xe_guc *
ads_to_guc(struct xe_guc_ads *ads)
{
	return container_of(ads, struct xe_guc, ads);
}

static struct xe_gt *
ads_to_gt(struct xe_guc_ads *ads)
{
	return container_of(ads, struct xe_gt, uc.guc.ads);
}

static struct xe_device *
ads_to_xe(struct xe_guc_ads *ads)
{
	return gt_to_xe(ads_to_gt(ads));
}

static struct iosys_map *
ads_to_map(struct xe_guc_ads *ads)
{
	return &ads->bo->vmap;
}

/* UM Queue parameters: */
#define GUC_UM_QUEUE_SIZE       (SZ_64K)
#define GUC_PAGE_RES_TIMEOUT_US (-1)

/*
 * The Additional Data Struct (ADS) has pointers for different buffers used by
 * the GuC. One single gem object contains the ADS struct itself (guc_ads) and
 * all the extra buffers indirectly linked via the ADS struct's entries.
 *
 * Layout of the ADS blob allocated for the GuC:
 *
 *      +---------------------------------------+ <== base
 *      | guc_ads                               |
 *      +---------------------------------------+
 *      | guc_policies                          |
 *      +---------------------------------------+
 *      | guc_gt_system_info                    |
 *      +---------------------------------------+
 *      | guc_engine_usage                      |
 *      +---------------------------------------+
 *      | guc_um_init_params                    |
 *      +---------------------------------------+ <== static
 *      | guc_mmio_reg[countA] (engine 0.0)     |
 *      | guc_mmio_reg[countB] (engine 0.1)     |
 *      | guc_mmio_reg[countC] (engine 1.0)     |
 *      |   ...                                 |
 *      +---------------------------------------+ <== dynamic
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | golden contexts                       |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | capture lists                         |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | UM queues                             |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 *      | private data                          |
 *      +---------------------------------------+
 *      | padding                               |
 *      +---------------------------------------+ <== 4K aligned
 */
struct __guc_ads_blob {
	struct guc_ads ads;
	struct guc_policies policies;
	struct guc_gt_system_info system_info;
	struct guc_engine_usage engine_usage;
	struct guc_um_init_params um_init_params;
	/* From here on, location is dynamic! Refer to above diagram. */
	struct guc_mmio_reg regset[];
} __packed;

#define ads_blob_read(ads_, field_) \
	xe_map_rd_field(ads_to_xe(ads_), ads_to_map(ads_), 0, \
			struct __guc_ads_blob, field_)

#define ads_blob_write(ads_, field_, val_)			\
	xe_map_wr_field(ads_to_xe(ads_), ads_to_map(ads_), 0,	\
			struct __guc_ads_blob, field_, val_)

#define info_map_write(xe_, map_, field_, val_) \
	xe_map_wr_field(xe_, map_, 0, struct guc_gt_system_info, field_, val_)

#define info_map_read(xe_, map_, field_) \
	xe_map_rd_field(xe_, map_, 0, struct guc_gt_system_info, field_)

static size_t guc_ads_regset_size(struct xe_guc_ads *ads)
{
	struct xe_device *xe = ads_to_xe(ads);

	xe_assert(xe, ads->regset_size);

	return ads->regset_size;
}

static size_t guc_ads_golden_lrc_size(struct xe_guc_ads *ads)
{
	return PAGE_ALIGN(ads->golden_lrc_size);
}

static size_t guc_ads_capture_size(struct xe_guc_ads *ads)
{
	/* FIXME: Allocate a proper capture list */
	return PAGE_ALIGN(PAGE_SIZE);
}

static size_t guc_ads_um_queues_size(struct xe_guc_ads *ads)
{
	struct xe_device *xe = ads_to_xe(ads);

	if (!xe->info.has_usm)
		return 0;

	return GUC_UM_QUEUE_SIZE * GUC_UM_HW_QUEUE_MAX;
}

static size_t guc_ads_private_data_size(struct xe_guc_ads *ads)
{
	return PAGE_ALIGN(ads_to_guc(ads)->fw.private_data_size);
}

static size_t guc_ads_regset_offset(struct xe_guc_ads *ads)
{
	return offsetof(struct __guc_ads_blob, regset);
}

static size_t guc_ads_golden_lrc_offset(struct xe_guc_ads *ads)
{
	size_t offset;

	offset = guc_ads_regset_offset(ads) +
		guc_ads_regset_size(ads);

	return PAGE_ALIGN(offset);
}

static size_t guc_ads_capture_offset(struct xe_guc_ads *ads)
{
	size_t offset;

	offset = guc_ads_golden_lrc_offset(ads) +
		guc_ads_golden_lrc_size(ads);

	return PAGE_ALIGN(offset);
}

static size_t guc_ads_um_queues_offset(struct xe_guc_ads *ads)
{
	u32 offset;

	offset = guc_ads_capture_offset(ads) +
		 guc_ads_capture_size(ads);

	return PAGE_ALIGN(offset);
}

static size_t guc_ads_private_data_offset(struct xe_guc_ads *ads)
{
	size_t offset;

	offset = guc_ads_um_queues_offset(ads) +
		guc_ads_um_queues_size(ads);

	return PAGE_ALIGN(offset);
}

static size_t guc_ads_size(struct xe_guc_ads *ads)
{
	return guc_ads_private_data_offset(ads) +
		guc_ads_private_data_size(ads);
}

static bool needs_wa_1607983814(struct xe_device *xe)
{
	return GRAPHICS_VERx100(xe) < 1250;
}

static size_t calculate_regset_size(struct xe_gt *gt)
{
	struct xe_reg_sr_entry *sr_entry;
	unsigned long sr_idx;
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	unsigned int count = 0;

	for_each_hw_engine(hwe, gt, id)
		xa_for_each(&hwe->reg_sr.xa, sr_idx, sr_entry)
			count++;

	count += ADS_REGSET_EXTRA_MAX * XE_NUM_HW_ENGINES;

	if (needs_wa_1607983814(gt_to_xe(gt)))
		count += LNCFCMOCS_REG_COUNT;

	return count * sizeof(struct guc_mmio_reg);
}

static u32 engine_enable_mask(struct xe_gt *gt, enum xe_engine_class class)
{
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	u32 mask = 0;

	for_each_hw_engine(hwe, gt, id)
		if (hwe->class == class)
			mask |= BIT(hwe->instance);

	return mask;
}

static size_t calculate_golden_lrc_size(struct xe_guc_ads *ads)
{
	struct xe_device *xe = ads_to_xe(ads);
	struct xe_gt *gt = ads_to_gt(ads);
	size_t total_size = 0, alloc_size, real_size;
	int class;

	for (class = 0; class < XE_ENGINE_CLASS_MAX; ++class) {
		if (!engine_enable_mask(gt, class))
			continue;

		real_size = xe_lrc_size(xe, class);
		alloc_size = PAGE_ALIGN(real_size);
		total_size += alloc_size;
	}

	return total_size;
}

#define MAX_GOLDEN_LRC_SIZE	(SZ_4K * 64)

int xe_guc_ads_init(struct xe_guc_ads *ads)
{
	struct xe_device *xe = ads_to_xe(ads);
	struct xe_gt *gt = ads_to_gt(ads);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_bo *bo;

	ads->golden_lrc_size = calculate_golden_lrc_size(ads);
	ads->regset_size = calculate_regset_size(gt);

	bo = xe_managed_bo_create_pin_map(xe, tile, guc_ads_size(ads) + MAX_GOLDEN_LRC_SIZE,
					  XE_BO_CREATE_SYSTEM_BIT |
					  XE_BO_CREATE_GGTT_BIT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	ads->bo = bo;

	return 0;
}

/**
 * xe_guc_ads_init_post_hwconfig - initialize ADS post hwconfig load
 * @ads: Additional data structures object
 *
 * Recalcuate golden_lrc_size & regset_size as the number hardware engines may
 * have changed after the hwconfig was loaded. Also verify the new sizes fit in
 * the already allocated ADS buffer object.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_guc_ads_init_post_hwconfig(struct xe_guc_ads *ads)
{
	struct xe_gt *gt = ads_to_gt(ads);
	u32 prev_regset_size = ads->regset_size;

	xe_gt_assert(gt, ads->bo);

	ads->golden_lrc_size = calculate_golden_lrc_size(ads);
	ads->regset_size = calculate_regset_size(gt);

	xe_gt_assert(gt, ads->golden_lrc_size +
		     (ads->regset_size - prev_regset_size) <=
		     MAX_GOLDEN_LRC_SIZE);

	return 0;
}

static void guc_policies_init(struct xe_guc_ads *ads)
{
	ads_blob_write(ads, policies.dpc_promote_time,
		       GLOBAL_POLICY_DEFAULT_DPC_PROMOTE_TIME_US);
	ads_blob_write(ads, policies.max_num_work_items,
		       GLOBAL_POLICY_MAX_NUM_WI);
	ads_blob_write(ads, policies.global_flags, 0);
	ads_blob_write(ads, policies.is_valid, 1);
}

static void fill_engine_enable_masks(struct xe_gt *gt,
				     struct iosys_map *info_map)
{
	struct xe_device *xe = gt_to_xe(gt);

	info_map_write(xe, info_map, engine_enabled_masks[GUC_RENDER_CLASS],
		       engine_enable_mask(gt, XE_ENGINE_CLASS_RENDER));
	info_map_write(xe, info_map, engine_enabled_masks[GUC_BLITTER_CLASS],
		       engine_enable_mask(gt, XE_ENGINE_CLASS_COPY));
	info_map_write(xe, info_map, engine_enabled_masks[GUC_VIDEO_CLASS],
		       engine_enable_mask(gt, XE_ENGINE_CLASS_VIDEO_DECODE));
	info_map_write(xe, info_map,
		       engine_enabled_masks[GUC_VIDEOENHANCE_CLASS],
		       engine_enable_mask(gt, XE_ENGINE_CLASS_VIDEO_ENHANCE));
	info_map_write(xe, info_map, engine_enabled_masks[GUC_COMPUTE_CLASS],
		       engine_enable_mask(gt, XE_ENGINE_CLASS_COMPUTE));
	info_map_write(xe, info_map, engine_enabled_masks[GUC_GSC_OTHER_CLASS],
		       engine_enable_mask(gt, XE_ENGINE_CLASS_OTHER));
}

static void guc_prep_golden_lrc_null(struct xe_guc_ads *ads)
{
	struct xe_device *xe = ads_to_xe(ads);
	struct iosys_map info_map = IOSYS_MAP_INIT_OFFSET(ads_to_map(ads),
			offsetof(struct __guc_ads_blob, system_info));
	u8 guc_class;

	for (guc_class = 0; guc_class <= GUC_MAX_ENGINE_CLASSES; ++guc_class) {
		if (!info_map_read(xe, &info_map,
				   engine_enabled_masks[guc_class]))
			continue;

		ads_blob_write(ads, ads.eng_state_size[guc_class],
			       guc_ads_golden_lrc_size(ads) -
			       xe_lrc_skip_size(xe));
		ads_blob_write(ads, ads.golden_context_lrca[guc_class],
			       xe_bo_ggtt_addr(ads->bo) +
			       guc_ads_golden_lrc_offset(ads));
	}
}

static void guc_mapping_table_init_invalid(struct xe_gt *gt,
					   struct iosys_map *info_map)
{
	struct xe_device *xe = gt_to_xe(gt);
	unsigned int i, j;

	/* Table must be set to invalid values for entries not used */
	for (i = 0; i < GUC_MAX_ENGINE_CLASSES; ++i)
		for (j = 0; j < GUC_MAX_INSTANCES_PER_CLASS; ++j)
			info_map_write(xe, info_map, mapping_table[i][j],
				       GUC_MAX_INSTANCES_PER_CLASS);
}

static void guc_mapping_table_init(struct xe_gt *gt,
				   struct iosys_map *info_map)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;

	guc_mapping_table_init_invalid(gt, info_map);

	for_each_hw_engine(hwe, gt, id) {
		u8 guc_class;

		guc_class = xe_engine_class_to_guc_class(hwe->class);
		info_map_write(xe, info_map,
			       mapping_table[guc_class][hwe->logical_instance],
			       hwe->instance);
	}
}

static void guc_capture_list_init(struct xe_guc_ads *ads)
{
	int i, j;
	u32 addr = xe_bo_ggtt_addr(ads->bo) + guc_ads_capture_offset(ads);

	/* FIXME: Populate a proper capture list */
	for (i = 0; i < GUC_CAPTURE_LIST_INDEX_MAX; i++) {
		for (j = 0; j < GUC_MAX_ENGINE_CLASSES; j++) {
			ads_blob_write(ads, ads.capture_instance[i][j], addr);
			ads_blob_write(ads, ads.capture_class[i][j], addr);
		}

		ads_blob_write(ads, ads.capture_global[i], addr);
	}
}

static void guc_mmio_regset_write_one(struct xe_guc_ads *ads,
				      struct iosys_map *regset_map,
				      struct xe_reg reg,
				      unsigned int n_entry)
{
	struct guc_mmio_reg entry = {
		.offset = reg.addr,
		.flags = reg.masked ? GUC_REGSET_MASKED : 0,
	};

	xe_map_memcpy_to(ads_to_xe(ads), regset_map, n_entry * sizeof(entry),
			 &entry, sizeof(entry));
}

static unsigned int guc_mmio_regset_write(struct xe_guc_ads *ads,
					  struct iosys_map *regset_map,
					  struct xe_hw_engine *hwe)
{
	struct xe_device *xe = ads_to_xe(ads);
	struct xe_hw_engine *hwe_rcs_reset_domain =
		xe_gt_any_hw_engine_by_reset_domain(hwe->gt, XE_ENGINE_CLASS_RENDER);
	struct xe_reg_sr_entry *entry;
	unsigned long idx;
	unsigned int count = 0;
	const struct {
		struct xe_reg reg;
		bool skip;
	} *e, extra_regs[] = {
		{ .reg = RING_MODE(hwe->mmio_base),			},
		{ .reg = RING_HWS_PGA(hwe->mmio_base),			},
		{ .reg = RING_IMR(hwe->mmio_base),			},
		{ .reg = RCU_MODE, .skip = hwe != hwe_rcs_reset_domain	},
		{ .reg = CCS_MODE,
		  .skip = hwe != hwe_rcs_reset_domain || !xe_gt_ccs_mode_enabled(hwe->gt) },
	};
	u32 i;

	BUILD_BUG_ON(ARRAY_SIZE(extra_regs) > ADS_REGSET_EXTRA_MAX);

	xa_for_each(&hwe->reg_sr.xa, idx, entry)
		guc_mmio_regset_write_one(ads, regset_map, entry->reg, count++);

	for (e = extra_regs; e < extra_regs + ARRAY_SIZE(extra_regs); e++) {
		if (e->skip)
			continue;

		guc_mmio_regset_write_one(ads, regset_map, e->reg, count++);
	}

	/* Wa_1607983814 */
	if (needs_wa_1607983814(xe) && hwe->class == XE_ENGINE_CLASS_RENDER) {
		for (i = 0; i < LNCFCMOCS_REG_COUNT; i++) {
			guc_mmio_regset_write_one(ads, regset_map,
						  XELP_LNCFCMOCS(i), count++);
		}
	}

	return count;
}

static void guc_mmio_reg_state_init(struct xe_guc_ads *ads)
{
	size_t regset_offset = guc_ads_regset_offset(ads);
	struct xe_gt *gt = ads_to_gt(ads);
	struct xe_hw_engine *hwe;
	enum xe_hw_engine_id id;
	u32 addr = xe_bo_ggtt_addr(ads->bo) + regset_offset;
	struct iosys_map regset_map = IOSYS_MAP_INIT_OFFSET(ads_to_map(ads),
							    regset_offset);
	unsigned int regset_used = 0;

	for_each_hw_engine(hwe, gt, id) {
		unsigned int count;
		u8 gc;

		/*
		 * 1. Write all MMIO entries for this exec queue to the table. No
		 * need to worry about fused-off engines and when there are
		 * entries in the regset: the reg_state_list has been zero'ed
		 * by xe_guc_ads_populate()
		 */
		count = guc_mmio_regset_write(ads, &regset_map, hwe);
		if (!count)
			continue;

		/*
		 * 2. Record in the header (ads.reg_state_list) the address
		 * location and number of entries
		 */
		gc = xe_engine_class_to_guc_class(hwe->class);
		ads_blob_write(ads, ads.reg_state_list[gc][hwe->instance].address, addr);
		ads_blob_write(ads, ads.reg_state_list[gc][hwe->instance].count, count);

		addr += count * sizeof(struct guc_mmio_reg);
		iosys_map_incr(&regset_map, count * sizeof(struct guc_mmio_reg));

		regset_used += count * sizeof(struct guc_mmio_reg);
	}

	xe_gt_assert(gt, regset_used <= ads->regset_size);
}

static void guc_um_init_params(struct xe_guc_ads *ads)
{
	u32 um_queue_offset = guc_ads_um_queues_offset(ads);
	u64 base_dpa;
	u32 base_ggtt;
	int i;

	base_ggtt = xe_bo_ggtt_addr(ads->bo) + um_queue_offset;
	base_dpa = xe_bo_main_addr(ads->bo, PAGE_SIZE) + um_queue_offset;

	for (i = 0; i < GUC_UM_HW_QUEUE_MAX; ++i) {
		ads_blob_write(ads, um_init_params.queue_params[i].base_dpa,
			       base_dpa + (i * GUC_UM_QUEUE_SIZE));
		ads_blob_write(ads, um_init_params.queue_params[i].base_ggtt_address,
			       base_ggtt + (i * GUC_UM_QUEUE_SIZE));
		ads_blob_write(ads, um_init_params.queue_params[i].size_in_bytes,
			       GUC_UM_QUEUE_SIZE);
	}

	ads_blob_write(ads, um_init_params.page_response_timeout_in_us,
		       GUC_PAGE_RES_TIMEOUT_US);
}

static void guc_doorbell_init(struct xe_guc_ads *ads)
{
	struct xe_device *xe = ads_to_xe(ads);
	struct xe_gt *gt = ads_to_gt(ads);

	if (GRAPHICS_VER(xe) >= 12 && !IS_DGFX(xe)) {
		u32 distdbreg =
			xe_mmio_read32(gt, DIST_DBS_POPULATED);

		ads_blob_write(ads,
			       system_info.generic_gt_sysinfo[GUC_GENERIC_GT_SYSINFO_DOORBELL_COUNT_PER_SQIDI],
			       REG_FIELD_GET(DOORBELLS_PER_SQIDI_MASK, distdbreg) + 1);
	}
}

/**
 * xe_guc_ads_populate_minimal - populate minimal ADS
 * @ads: Additional data structures object
 *
 * This function populates a minimal ADS that does not support submissions but
 * enough so the GuC can load and the hwconfig table can be read.
 */
void xe_guc_ads_populate_minimal(struct xe_guc_ads *ads)
{
	struct xe_gt *gt = ads_to_gt(ads);
	struct iosys_map info_map = IOSYS_MAP_INIT_OFFSET(ads_to_map(ads),
			offsetof(struct __guc_ads_blob, system_info));
	u32 base = xe_bo_ggtt_addr(ads->bo);

	xe_gt_assert(gt, ads->bo);

	xe_map_memset(ads_to_xe(ads), ads_to_map(ads), 0, 0, ads->bo->size);
	guc_policies_init(ads);
	guc_prep_golden_lrc_null(ads);
	guc_mapping_table_init_invalid(gt, &info_map);
	guc_doorbell_init(ads);

	ads_blob_write(ads, ads.scheduler_policies, base +
		       offsetof(struct __guc_ads_blob, policies));
	ads_blob_write(ads, ads.gt_system_info, base +
		       offsetof(struct __guc_ads_blob, system_info));
	ads_blob_write(ads, ads.private_data, base +
		       guc_ads_private_data_offset(ads));
}

void xe_guc_ads_populate(struct xe_guc_ads *ads)
{
	struct xe_device *xe = ads_to_xe(ads);
	struct xe_gt *gt = ads_to_gt(ads);
	struct iosys_map info_map = IOSYS_MAP_INIT_OFFSET(ads_to_map(ads),
			offsetof(struct __guc_ads_blob, system_info));
	u32 base = xe_bo_ggtt_addr(ads->bo);

	xe_gt_assert(gt, ads->bo);

	xe_map_memset(ads_to_xe(ads), ads_to_map(ads), 0, 0, ads->bo->size);
	guc_policies_init(ads);
	fill_engine_enable_masks(gt, &info_map);
	guc_mmio_reg_state_init(ads);
	guc_prep_golden_lrc_null(ads);
	guc_mapping_table_init(gt, &info_map);
	guc_capture_list_init(ads);
	guc_doorbell_init(ads);

	if (xe->info.has_usm) {
		guc_um_init_params(ads);
		ads_blob_write(ads, ads.um_init_data, base +
			       offsetof(struct __guc_ads_blob, um_init_params));
	}

	ads_blob_write(ads, ads.scheduler_policies, base +
		       offsetof(struct __guc_ads_blob, policies));
	ads_blob_write(ads, ads.gt_system_info, base +
		       offsetof(struct __guc_ads_blob, system_info));
	ads_blob_write(ads, ads.private_data, base +
		       guc_ads_private_data_offset(ads));
}

static void guc_populate_golden_lrc(struct xe_guc_ads *ads)
{
	struct xe_device *xe = ads_to_xe(ads);
	struct xe_gt *gt = ads_to_gt(ads);
	struct iosys_map info_map = IOSYS_MAP_INIT_OFFSET(ads_to_map(ads),
			offsetof(struct __guc_ads_blob, system_info));
	size_t total_size = 0, alloc_size, real_size;
	u32 addr_ggtt, offset;
	int class;

	offset = guc_ads_golden_lrc_offset(ads);
	addr_ggtt = xe_bo_ggtt_addr(ads->bo) + offset;

	for (class = 0; class < XE_ENGINE_CLASS_MAX; ++class) {
		u8 guc_class;

		guc_class = xe_engine_class_to_guc_class(class);

		if (!info_map_read(xe, &info_map,
				   engine_enabled_masks[guc_class]))
			continue;

		xe_gt_assert(gt, gt->default_lrc[class]);

		real_size = xe_lrc_size(xe, class);
		alloc_size = PAGE_ALIGN(real_size);
		total_size += alloc_size;

		/*
		 * This interface is slightly confusing. We need to pass the
		 * base address of the full golden context and the size of just
		 * the engine state, which is the section of the context image
		 * that starts after the execlists LRC registers. This is
		 * required to allow the GuC to restore just the engine state
		 * when a watchdog reset occurs.
		 * We calculate the engine state size by removing the size of
		 * what comes before it in the context image (which is identical
		 * on all engines).
		 */
		ads_blob_write(ads, ads.eng_state_size[guc_class],
			       real_size - xe_lrc_skip_size(xe));
		ads_blob_write(ads, ads.golden_context_lrca[guc_class],
			       addr_ggtt);

		xe_map_memcpy_to(xe, ads_to_map(ads), offset,
				 gt->default_lrc[class], real_size);

		addr_ggtt += alloc_size;
		offset += alloc_size;
	}

	xe_gt_assert(gt, total_size == ads->golden_lrc_size);
}

void xe_guc_ads_populate_post_load(struct xe_guc_ads *ads)
{
	guc_populate_golden_lrc(ads);
}
