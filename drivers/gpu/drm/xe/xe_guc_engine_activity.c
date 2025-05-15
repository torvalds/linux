// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "abi/guc_actions_abi.h"
#include "regs/xe_gt_regs.h"

#include "xe_bo.h"
#include "xe_force_wake.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_engine_activity.h"
#include "xe_guc_ct.h"
#include "xe_hw_engine.h"
#include "xe_map.h"
#include "xe_mmio.h"
#include "xe_trace_guc.h"

#define TOTAL_QUANTA 0x8000

static struct iosys_map engine_activity_map(struct xe_guc *guc, struct xe_hw_engine *hwe)
{
	struct xe_guc_engine_activity *engine_activity = &guc->engine_activity;
	struct engine_activity_buffer *buffer = &engine_activity->device_buffer;
	u16 guc_class = xe_engine_class_to_guc_class(hwe->class);
	size_t offset;

	offset = offsetof(struct guc_engine_activity_data,
			  engine_activity[guc_class][hwe->logical_instance]);

	return IOSYS_MAP_INIT_OFFSET(&buffer->activity_bo->vmap, offset);
}

static struct iosys_map engine_metadata_map(struct xe_guc *guc)
{
	struct xe_guc_engine_activity *engine_activity = &guc->engine_activity;
	struct engine_activity_buffer *buffer = &engine_activity->device_buffer;

	return buffer->metadata_bo->vmap;
}

static int allocate_engine_activity_group(struct xe_guc *guc)
{
	struct xe_guc_engine_activity *engine_activity = &guc->engine_activity;
	struct xe_device *xe = guc_to_xe(guc);
	u32 num_activity_group = 1; /* Will be modified for VF */

	engine_activity->eag  = drmm_kcalloc(&xe->drm, num_activity_group,
					     sizeof(struct engine_activity_group), GFP_KERNEL);

	if (!engine_activity->eag)
		return -ENOMEM;

	engine_activity->num_activity_group = num_activity_group;

	return 0;
}

static int allocate_engine_activity_buffers(struct xe_guc *guc,
					    struct engine_activity_buffer *buffer)
{
	u32 metadata_size = sizeof(struct guc_engine_activity_metadata);
	u32 size = sizeof(struct guc_engine_activity_data);
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_bo *bo, *metadata_bo;

	metadata_bo = xe_bo_create_pin_map(gt_to_xe(gt), tile, NULL, PAGE_ALIGN(metadata_size),
					   ttm_bo_type_kernel, XE_BO_FLAG_SYSTEM |
					   XE_BO_FLAG_GGTT | XE_BO_FLAG_GGTT_INVALIDATE);

	if (IS_ERR(metadata_bo))
		return PTR_ERR(metadata_bo);

	bo = xe_bo_create_pin_map(gt_to_xe(gt), tile, NULL, PAGE_ALIGN(size),
				  ttm_bo_type_kernel, XE_BO_FLAG_VRAM_IF_DGFX(tile) |
				  XE_BO_FLAG_GGTT | XE_BO_FLAG_GGTT_INVALIDATE);

	if (IS_ERR(bo)) {
		xe_bo_unpin_map_no_vm(metadata_bo);
		return PTR_ERR(bo);
	}

	buffer->metadata_bo = metadata_bo;
	buffer->activity_bo = bo;
	return 0;
}

static void free_engine_activity_buffers(struct engine_activity_buffer *buffer)
{
	xe_bo_unpin_map_no_vm(buffer->metadata_bo);
	xe_bo_unpin_map_no_vm(buffer->activity_bo);
}

static bool is_engine_activity_supported(struct xe_guc *guc)
{
	struct xe_uc_fw_version *version = &guc->fw.versions.found[XE_UC_FW_VER_COMPATIBILITY];
	struct xe_uc_fw_version required = { 1, 14, 1 };
	struct xe_gt *gt = guc_to_gt(guc);

	if (IS_SRIOV_VF(gt_to_xe(gt))) {
		xe_gt_info(gt, "engine activity stats not supported on VFs\n");
		return false;
	}

	/* engine activity stats is supported from GuC interface version (1.14.1) */
	if (GUC_SUBMIT_VER(guc) < MAKE_GUC_VER_STRUCT(required)) {
		xe_gt_info(gt,
			   "engine activity stats unsupported in GuC interface v%u.%u.%u, need v%u.%u.%u or higher\n",
			   version->major, version->minor, version->patch, required.major,
			   required.minor, required.patch);
		return false;
	}

	return true;
}

static struct engine_activity *hw_engine_to_engine_activity(struct xe_hw_engine *hwe)
{
	struct xe_guc *guc = &hwe->gt->uc.guc;
	struct engine_activity_group *eag = &guc->engine_activity.eag[0];
	u16 guc_class = xe_engine_class_to_guc_class(hwe->class);

	return &eag->engine[guc_class][hwe->logical_instance];
}

static u64 cpu_ns_to_guc_tsc_tick(ktime_t ns, u32 freq)
{
	return mul_u64_u32_div(ns, freq, NSEC_PER_SEC);
}

#define read_engine_activity_record(xe_, map_, field_) \
	xe_map_rd_field(xe_, map_, 0, struct guc_engine_activity, field_)

#define read_metadata_record(xe_, map_, field_) \
	xe_map_rd_field(xe_, map_, 0, struct guc_engine_activity_metadata, field_)

static u64 get_engine_active_ticks(struct xe_guc *guc, struct xe_hw_engine *hwe)
{
	struct engine_activity *ea = hw_engine_to_engine_activity(hwe);
	struct guc_engine_activity *cached_activity = &ea->activity;
	struct guc_engine_activity_metadata *cached_metadata = &ea->metadata;
	struct xe_guc_engine_activity *engine_activity = &guc->engine_activity;
	struct iosys_map activity_map, metadata_map;
	struct xe_device *xe =  guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);
	u32 last_update_tick, global_change_num;
	u64 active_ticks, gpm_ts;
	u16 change_num;

	activity_map = engine_activity_map(guc, hwe);
	metadata_map = engine_metadata_map(guc);
	global_change_num = read_metadata_record(xe, &metadata_map, global_change_num);

	/* GuC has not initialized activity data yet, return 0 */
	if (!global_change_num)
		goto update;

	if (global_change_num == cached_metadata->global_change_num)
		goto update;

	cached_metadata->global_change_num = global_change_num;
	change_num = read_engine_activity_record(xe, &activity_map, change_num);

	if (!change_num || change_num == cached_activity->change_num)
		goto update;

	/* read engine activity values */
	last_update_tick = read_engine_activity_record(xe, &activity_map, last_update_tick);
	active_ticks = read_engine_activity_record(xe, &activity_map, active_ticks);

	/* activity calculations */
	ea->running = !!last_update_tick;
	ea->total += active_ticks - cached_activity->active_ticks;
	ea->active = 0;

	/* cache the counter */
	cached_activity->change_num = change_num;
	cached_activity->last_update_tick = last_update_tick;
	cached_activity->active_ticks = active_ticks;

update:
	if (ea->running) {
		gpm_ts = xe_mmio_read64_2x32(&gt->mmio, MISC_STATUS_0) >>
			 engine_activity->gpm_timestamp_shift;
		ea->active = lower_32_bits(gpm_ts) - cached_activity->last_update_tick;
	}

	trace_xe_guc_engine_activity(xe, ea, hwe->name, hwe->instance);

	return ea->total + ea->active;
}

static u64 get_engine_total_ticks(struct xe_guc *guc, struct xe_hw_engine *hwe)
{
	struct engine_activity *ea = hw_engine_to_engine_activity(hwe);
	struct guc_engine_activity_metadata *cached_metadata = &ea->metadata;
	struct guc_engine_activity *cached_activity = &ea->activity;
	struct iosys_map activity_map, metadata_map;
	struct xe_device *xe = guc_to_xe(guc);
	ktime_t now, cpu_delta;
	u64 numerator;
	u16 quanta_ratio;

	activity_map = engine_activity_map(guc, hwe);
	metadata_map = engine_metadata_map(guc);

	if (!cached_metadata->guc_tsc_frequency_hz)
		cached_metadata->guc_tsc_frequency_hz = read_metadata_record(xe, &metadata_map,
									     guc_tsc_frequency_hz);

	quanta_ratio = read_engine_activity_record(xe, &activity_map, quanta_ratio);
	cached_activity->quanta_ratio = quanta_ratio;

	/* Total ticks calculations */
	now = ktime_get();
	cpu_delta = now - ea->last_cpu_ts;
	ea->last_cpu_ts = now;
	numerator = (ea->quanta_remainder_ns + cpu_delta) * cached_activity->quanta_ratio;
	ea->quanta_ns += numerator / TOTAL_QUANTA;
	ea->quanta_remainder_ns = numerator % TOTAL_QUANTA;
	ea->quanta = cpu_ns_to_guc_tsc_tick(ea->quanta_ns, cached_metadata->guc_tsc_frequency_hz);

	trace_xe_guc_engine_activity(xe, ea, hwe->name, hwe->instance);

	return ea->quanta;
}

static int enable_engine_activity_stats(struct xe_guc *guc)
{
	struct xe_guc_engine_activity *engine_activity = &guc->engine_activity;
	struct engine_activity_buffer *buffer = &engine_activity->device_buffer;
	u32 action[] = {
		XE_GUC_ACTION_SET_DEVICE_ENGINE_ACTIVITY_BUFFER,
		xe_bo_ggtt_addr(buffer->metadata_bo),
		0,
		xe_bo_ggtt_addr(buffer->activity_bo),
		0,
	};

	/* Blocking here to ensure the buffers are ready before reading them */
	return xe_guc_ct_send_block(&guc->ct, action, ARRAY_SIZE(action));
}

static void engine_activity_set_cpu_ts(struct xe_guc *guc)
{
	struct xe_guc_engine_activity *engine_activity = &guc->engine_activity;
	struct engine_activity_group *eag = &engine_activity->eag[0];
	int i, j;

	for (i = 0; i < GUC_MAX_ENGINE_CLASSES; i++)
		for (j = 0; j < GUC_MAX_INSTANCES_PER_CLASS; j++)
			eag->engine[i][j].last_cpu_ts = ktime_get();
}

static u32 gpm_timestamp_shift(struct xe_gt *gt)
{
	u32 reg;

	reg = xe_mmio_read32(&gt->mmio, RPM_CONFIG0);

	return 3 - REG_FIELD_GET(RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK, reg);
}

/**
 * xe_guc_engine_activity_active_ticks - Get engine active ticks
 * @guc: The GuC object
 * @hwe: The hw_engine object
 *
 * Return: accumulated ticks @hwe was active since engine activity stats were enabled.
 */
u64 xe_guc_engine_activity_active_ticks(struct xe_guc *guc, struct xe_hw_engine *hwe)
{
	if (!xe_guc_engine_activity_supported(guc))
		return 0;

	return get_engine_active_ticks(guc, hwe);
}

/**
 * xe_guc_engine_activity_total_ticks - Get engine total ticks
 * @guc: The GuC object
 * @hwe: The hw_engine object
 *
 * Return: accumulated quanta of ticks allocated for the engine
 */
u64 xe_guc_engine_activity_total_ticks(struct xe_guc *guc, struct xe_hw_engine *hwe)
{
	if (!xe_guc_engine_activity_supported(guc))
		return 0;

	return get_engine_total_ticks(guc, hwe);
}

/**
 * xe_guc_engine_activity_supported - Check support for engine activity stats
 * @guc: The GuC object
 *
 * Engine activity stats is supported from GuC interface version (1.14.1)
 *
 * Return: true if engine activity stats supported, false otherwise
 */
bool xe_guc_engine_activity_supported(struct xe_guc *guc)
{
	struct xe_guc_engine_activity *engine_activity = &guc->engine_activity;

	return engine_activity->supported;
}

/**
 * xe_guc_engine_activity_enable_stats - Enable engine activity stats
 * @guc: The GuC object
 *
 * Enable engine activity stats and set initial timestamps
 */
void xe_guc_engine_activity_enable_stats(struct xe_guc *guc)
{
	int ret;

	if (!xe_guc_engine_activity_supported(guc))
		return;

	ret = enable_engine_activity_stats(guc);
	if (ret)
		xe_gt_err(guc_to_gt(guc), "failed to enable activity stats%d\n", ret);
	else
		engine_activity_set_cpu_ts(guc);
}

static void engine_activity_fini(void *arg)
{
	struct xe_guc_engine_activity *engine_activity = arg;
	struct engine_activity_buffer *buffer = &engine_activity->device_buffer;

	free_engine_activity_buffers(buffer);
}

/**
 * xe_guc_engine_activity_init - Initialize the engine activity data
 * @guc: The GuC object
 *
 * Return: 0 on success, negative error code otherwise.
 */
int xe_guc_engine_activity_init(struct xe_guc *guc)
{
	struct xe_guc_engine_activity *engine_activity = &guc->engine_activity;
	struct xe_gt *gt = guc_to_gt(guc);
	int ret;

	engine_activity->supported = is_engine_activity_supported(guc);
	if (!engine_activity->supported)
		return 0;

	ret = allocate_engine_activity_group(guc);
	if (ret) {
		xe_gt_err(gt, "failed to allocate engine activity group (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	ret = allocate_engine_activity_buffers(guc, &engine_activity->device_buffer);
	if (ret) {
		xe_gt_err(gt, "failed to allocate engine activity buffers (%pe)\n", ERR_PTR(ret));
		return ret;
	}

	engine_activity->gpm_timestamp_shift = gpm_timestamp_shift(gt);

	return devm_add_action_or_reset(gt_to_xe(gt)->drm.dev, engine_activity_fini,
					engine_activity);
}
