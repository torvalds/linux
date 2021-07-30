// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_guc_slpc.h"
#include "gt/intel_gt.h"

static inline struct intel_guc *slpc_to_guc(struct intel_guc_slpc *slpc)
{
	return container_of(slpc, struct intel_guc, slpc);
}

static inline struct intel_gt *slpc_to_gt(struct intel_guc_slpc *slpc)
{
	return guc_to_gt(slpc_to_guc(slpc));
}

static inline struct drm_i915_private *slpc_to_i915(struct intel_guc_slpc *slpc)
{
	return slpc_to_gt(slpc)->i915;
}

static bool __detect_slpc_supported(struct intel_guc *guc)
{
	/* GuC SLPC is unavailable for pre-Gen12 */
	return guc->submission_supported &&
		GRAPHICS_VER(guc_to_gt(guc)->i915) >= 12;
}

static bool __guc_slpc_selected(struct intel_guc *guc)
{
	if (!intel_guc_slpc_is_supported(guc))
		return false;

	return guc->submission_selected;
}

void intel_guc_slpc_init_early(struct intel_guc_slpc *slpc)
{
	struct intel_guc *guc = slpc_to_guc(slpc);

	slpc->supported = __detect_slpc_supported(guc);
	slpc->selected = __guc_slpc_selected(guc);
}

static void slpc_mem_set_param(struct slpc_shared_data *data,
			       u32 id, u32 value)
{
	GEM_BUG_ON(id >= SLPC_MAX_OVERRIDE_PARAMETERS);
	/*
	 * When the flag bit is set, corresponding value will be read
	 * and applied by SLPC.
	 */
	data->override_params.bits[id >> 5] |= (1 << (id % 32));
	data->override_params.values[id] = value;
}

static void slpc_mem_set_enabled(struct slpc_shared_data *data,
				 u8 enable_id, u8 disable_id)
{
	/*
	 * Enabling a param involves setting the enable_id
	 * to 1 and disable_id to 0.
	 */
	slpc_mem_set_param(data, enable_id, 1);
	slpc_mem_set_param(data, disable_id, 0);
}

static void slpc_mem_set_disabled(struct slpc_shared_data *data,
				  u8 enable_id, u8 disable_id)
{
	/*
	 * Disabling a param involves setting the enable_id
	 * to 0 and disable_id to 1.
	 */
	slpc_mem_set_param(data, disable_id, 1);
	slpc_mem_set_param(data, enable_id, 0);
}

int intel_guc_slpc_init(struct intel_guc_slpc *slpc)
{
	struct intel_guc *guc = slpc_to_guc(slpc);
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	u32 size = PAGE_ALIGN(sizeof(struct slpc_shared_data));
	int err;

	GEM_BUG_ON(slpc->vma);

	err = intel_guc_allocate_and_map_vma(guc, size, &slpc->vma, (void **)&slpc->vaddr);
	if (unlikely(err)) {
		drm_err(&i915->drm,
			"Failed to allocate SLPC struct (err=%pe)\n",
			ERR_PTR(err));
		return err;
	}

	return err;
}

static u32 slpc_get_state(struct intel_guc_slpc *slpc)
{
	struct slpc_shared_data *data;

	GEM_BUG_ON(!slpc->vma);

	drm_clflush_virt_range(slpc->vaddr, sizeof(u32));
	data = slpc->vaddr;

	return data->header.global_state;
}

static int guc_action_slpc_set_param(struct intel_guc *guc, u8 id, u32 value)
{
	u32 request[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_PARAMETER_SET, 2),
		id,
		value,
	};
	int ret;

	ret = intel_guc_send(guc, request, ARRAY_SIZE(request));

	return ret > 0 ? -EPROTO : ret;
}

static bool slpc_is_running(struct intel_guc_slpc *slpc)
{
	return slpc_get_state(slpc) == SLPC_GLOBAL_STATE_RUNNING;
}

static int guc_action_slpc_query(struct intel_guc *guc, u32 offset)
{
	u32 request[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_QUERY_TASK_STATE, 2),
		offset,
		0,
	};
	int ret;

	ret = intel_guc_send(guc, request, ARRAY_SIZE(request));

	return ret > 0 ? -EPROTO : ret;
}

static int slpc_query_task_state(struct intel_guc_slpc *slpc)
{
	struct intel_guc *guc = slpc_to_guc(slpc);
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	u32 offset = intel_guc_ggtt_offset(guc, slpc->vma);
	int ret;

	ret = guc_action_slpc_query(guc, offset);
	if (unlikely(ret))
		drm_err(&i915->drm, "Failed to query task state (%pe)\n",
			ERR_PTR(ret));

	drm_clflush_virt_range(slpc->vaddr, SLPC_PAGE_SIZE_BYTES);

	return ret;
}

static int slpc_set_param(struct intel_guc_slpc *slpc, u8 id, u32 value)
{
	struct intel_guc *guc = slpc_to_guc(slpc);
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	int ret;

	GEM_BUG_ON(id >= SLPC_MAX_PARAM);

	ret = guc_action_slpc_set_param(guc, id, value);
	if (ret)
		drm_err(&i915->drm, "Failed to set param %d to %u (%pe)\n",
			id, value, ERR_PTR(ret));

	return ret;
}

static const char *slpc_global_state_to_string(enum slpc_global_state state)
{
	switch (state) {
	case SLPC_GLOBAL_STATE_NOT_RUNNING:
		return "not running";
	case SLPC_GLOBAL_STATE_INITIALIZING:
		return "initializing";
	case SLPC_GLOBAL_STATE_RESETTING:
		return "resetting";
	case SLPC_GLOBAL_STATE_RUNNING:
		return "running";
	case SLPC_GLOBAL_STATE_SHUTTING_DOWN:
		return "shutting down";
	case SLPC_GLOBAL_STATE_ERROR:
		return "error";
	default:
		return "unknown";
	}
}

static const char *slpc_get_state_string(struct intel_guc_slpc *slpc)
{
	return slpc_global_state_to_string(slpc_get_state(slpc));
}

static int guc_action_slpc_reset(struct intel_guc *guc, u32 offset)
{
	u32 request[] = {
		GUC_ACTION_HOST2GUC_PC_SLPC_REQUEST,
		SLPC_EVENT(SLPC_EVENT_RESET, 2),
		offset,
		0,
	};
	int ret;

	ret = intel_guc_send(guc, request, ARRAY_SIZE(request));

	return ret > 0 ? -EPROTO : ret;
}

static int slpc_reset(struct intel_guc_slpc *slpc)
{
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	struct intel_guc *guc = slpc_to_guc(slpc);
	u32 offset = intel_guc_ggtt_offset(guc, slpc->vma);
	int ret;

	ret = guc_action_slpc_reset(guc, offset);

	if (unlikely(ret < 0)) {
		drm_err(&i915->drm, "SLPC reset action failed (%pe)\n",
			ERR_PTR(ret));
		return ret;
	}

	if (!ret) {
		if (wait_for(slpc_is_running(slpc), SLPC_RESET_TIMEOUT_MS)) {
			drm_err(&i915->drm, "SLPC not enabled! State = %s\n",
				slpc_get_state_string(slpc));
			return -EIO;
		}
	}

	return 0;
}

static u32 slpc_decode_min_freq(struct intel_guc_slpc *slpc)
{
	struct slpc_shared_data *data = slpc->vaddr;

	GEM_BUG_ON(!slpc->vma);

	return	DIV_ROUND_CLOSEST(REG_FIELD_GET(SLPC_MIN_UNSLICE_FREQ_MASK,
				  data->task_state_data.freq) *
				  GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER);
}

static u32 slpc_decode_max_freq(struct intel_guc_slpc *slpc)
{
	struct slpc_shared_data *data = slpc->vaddr;

	GEM_BUG_ON(!slpc->vma);

	return	DIV_ROUND_CLOSEST(REG_FIELD_GET(SLPC_MAX_UNSLICE_FREQ_MASK,
				  data->task_state_data.freq) *
				  GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER);
}

static void slpc_shared_data_reset(struct slpc_shared_data *data)
{
	memset(data, 0, sizeof(struct slpc_shared_data));

	data->header.size = sizeof(struct slpc_shared_data);

	/* Enable only GTPERF task, disable others */
	slpc_mem_set_enabled(data, SLPC_PARAM_TASK_ENABLE_GTPERF,
			     SLPC_PARAM_TASK_DISABLE_GTPERF);

	slpc_mem_set_disabled(data, SLPC_PARAM_TASK_ENABLE_BALANCER,
			      SLPC_PARAM_TASK_DISABLE_BALANCER);

	slpc_mem_set_disabled(data, SLPC_PARAM_TASK_ENABLE_DCC,
			      SLPC_PARAM_TASK_DISABLE_DCC);
}

/**
 * intel_guc_slpc_set_max_freq() - Set max frequency limit for SLPC.
 * @slpc: pointer to intel_guc_slpc.
 * @val: frequency (MHz)
 *
 * This function will invoke GuC SLPC action to update the max frequency
 * limit for unslice.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_guc_slpc_set_max_freq(struct intel_guc_slpc *slpc, u32 val)
{
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	intel_wakeref_t wakeref;
	int ret;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		ret = slpc_set_param(slpc,
				     SLPC_PARAM_GLOBAL_MAX_GT_UNSLICE_FREQ_MHZ,
				     val);

		/* Return standardized err code for sysfs calls */
		if (ret)
			ret = -EIO;
	}

	return ret;
}

/**
 * intel_guc_slpc_get_max_freq() - Get max frequency limit for SLPC.
 * @slpc: pointer to intel_guc_slpc.
 * @val: pointer to val which will hold max frequency (MHz)
 *
 * This function will invoke GuC SLPC action to read the max frequency
 * limit for unslice.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_guc_slpc_get_max_freq(struct intel_guc_slpc *slpc, u32 *val)
{
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	intel_wakeref_t wakeref;
	int ret = 0;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		/* Force GuC to update task data */
		ret = slpc_query_task_state(slpc);

		if (!ret)
			*val = slpc_decode_max_freq(slpc);
	}

	return ret;
}

/**
 * intel_guc_slpc_set_min_freq() - Set min frequency limit for SLPC.
 * @slpc: pointer to intel_guc_slpc.
 * @val: frequency (MHz)
 *
 * This function will invoke GuC SLPC action to update the min unslice
 * frequency.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_guc_slpc_set_min_freq(struct intel_guc_slpc *slpc, u32 val)
{
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	intel_wakeref_t wakeref;
	int ret;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		ret = slpc_set_param(slpc,
				     SLPC_PARAM_GLOBAL_MIN_GT_UNSLICE_FREQ_MHZ,
				     val);

		/* Return standardized err code for sysfs calls */
		if (ret)
			ret = -EIO;
	}

	return ret;
}

/**
 * intel_guc_slpc_get_min_freq() - Get min frequency limit for SLPC.
 * @slpc: pointer to intel_guc_slpc.
 * @val: pointer to val which will hold min frequency (MHz)
 *
 * This function will invoke GuC SLPC action to read the min frequency
 * limit for unslice.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_guc_slpc_get_min_freq(struct intel_guc_slpc *slpc, u32 *val)
{
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	intel_wakeref_t wakeref;
	int ret = 0;

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		/* Force GuC to update task data */
		ret = slpc_query_task_state(slpc);

		if (!ret)
			*val = slpc_decode_min_freq(slpc);
	}

	return ret;
}

void intel_guc_pm_intrmsk_enable(struct intel_gt *gt)
{
	u32 pm_intrmsk_mbz = 0;

	/*
	 * Allow GuC to receive ARAT timer expiry event.
	 * This interrupt register is setup by RPS code
	 * when host based Turbo is enabled.
	 */
	pm_intrmsk_mbz |= ARAT_EXPIRED_INTRMSK;

	intel_uncore_rmw(gt->uncore,
			 GEN6_PMINTRMSK, pm_intrmsk_mbz, 0);
}

/*
 * intel_guc_slpc_enable() - Start SLPC
 * @slpc: pointer to intel_guc_slpc.
 *
 * SLPC is enabled by setting up the shared data structure and
 * sending reset event to GuC SLPC. Initial data is setup in
 * intel_guc_slpc_init. Here we send the reset event. We do
 * not currently need a slpc_disable since this is taken care
 * of automatically when a reset/suspend occurs and the GuC
 * CTB is destroyed.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_guc_slpc_enable(struct intel_guc_slpc *slpc)
{
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	int ret;

	GEM_BUG_ON(!slpc->vma);

	slpc_shared_data_reset(slpc->vaddr);

	ret = slpc_reset(slpc);
	if (unlikely(ret < 0)) {
		drm_err(&i915->drm, "SLPC Reset event returned (%pe)\n",
			ERR_PTR(ret));
		return ret;
	}

	ret = slpc_query_task_state(slpc);
	if (unlikely(ret < 0))
		return ret;

	intel_guc_pm_intrmsk_enable(&i915->gt);

	return 0;
}

int intel_guc_slpc_print_info(struct intel_guc_slpc *slpc, struct drm_printer *p)
{
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	struct slpc_shared_data *data = slpc->vaddr;
	struct slpc_task_state_data *slpc_tasks;
	intel_wakeref_t wakeref;
	int ret = 0;

	GEM_BUG_ON(!slpc->vma);

	with_intel_runtime_pm(&i915->runtime_pm, wakeref) {
		ret = slpc_query_task_state(slpc);

		if (!ret) {
			slpc_tasks = &data->task_state_data;

			drm_printf(p, "\tSLPC state: %s\n", slpc_get_state_string(slpc));
			drm_printf(p, "\tGTPERF task active: %s\n",
				   yesno(slpc_tasks->status & SLPC_GTPERF_TASK_ENABLED));
			drm_printf(p, "\tMax freq: %u MHz\n",
				   slpc_decode_max_freq(slpc));
			drm_printf(p, "\tMin freq: %u MHz\n",
				   slpc_decode_min_freq(slpc));
		}
	}

	return ret;
}

void intel_guc_slpc_fini(struct intel_guc_slpc *slpc)
{
	if (!slpc->vma)
		return;

	i915_vma_unpin_and_release(&slpc->vma, I915_VMA_RELEASE_MAP);
}
