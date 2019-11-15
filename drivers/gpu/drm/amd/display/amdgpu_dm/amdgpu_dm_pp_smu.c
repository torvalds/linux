/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 */
#include <linux/string.h>
#include <linux/acpi.h>

#include <drm/drm_probe_helper.h>
#include <drm/amdgpu_drm.h>
#include "dm_services.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_pm.h"
#include "dm_pp_smu.h"
#include "amdgpu_smu.h"


bool dm_pp_apply_display_requirements(
		const struct dc_context *ctx,
		const struct dm_pp_display_configuration *pp_display_cfg)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;
	int i;

	if (adev->pm.dpm_enabled) {

		memset(&adev->pm.pm_display_cfg, 0,
				sizeof(adev->pm.pm_display_cfg));

		adev->pm.pm_display_cfg.cpu_cc6_disable =
			pp_display_cfg->cpu_cc6_disable;

		adev->pm.pm_display_cfg.cpu_pstate_disable =
			pp_display_cfg->cpu_pstate_disable;

		adev->pm.pm_display_cfg.cpu_pstate_separation_time =
			pp_display_cfg->cpu_pstate_separation_time;

		adev->pm.pm_display_cfg.nb_pstate_switch_disable =
			pp_display_cfg->nb_pstate_switch_disable;

		adev->pm.pm_display_cfg.num_display =
				pp_display_cfg->display_count;
		adev->pm.pm_display_cfg.num_path_including_non_display =
				pp_display_cfg->display_count;

		adev->pm.pm_display_cfg.min_core_set_clock =
				pp_display_cfg->min_engine_clock_khz/10;
		adev->pm.pm_display_cfg.min_core_set_clock_in_sr =
				pp_display_cfg->min_engine_clock_deep_sleep_khz/10;
		adev->pm.pm_display_cfg.min_mem_set_clock =
				pp_display_cfg->min_memory_clock_khz/10;

		adev->pm.pm_display_cfg.min_dcef_deep_sleep_set_clk =
				pp_display_cfg->min_engine_clock_deep_sleep_khz/10;
		adev->pm.pm_display_cfg.min_dcef_set_clk =
				pp_display_cfg->min_dcfclock_khz/10;

		adev->pm.pm_display_cfg.multi_monitor_in_sync =
				pp_display_cfg->all_displays_in_sync;
		adev->pm.pm_display_cfg.min_vblank_time =
				pp_display_cfg->avail_mclk_switch_time_us;

		adev->pm.pm_display_cfg.display_clk =
				pp_display_cfg->disp_clk_khz/10;

		adev->pm.pm_display_cfg.dce_tolerable_mclk_in_active_latency =
				pp_display_cfg->avail_mclk_switch_time_in_disp_active_us;

		adev->pm.pm_display_cfg.crtc_index = pp_display_cfg->crtc_index;
		adev->pm.pm_display_cfg.line_time_in_us =
				pp_display_cfg->line_time_in_us;

		adev->pm.pm_display_cfg.vrefresh = pp_display_cfg->disp_configs[0].v_refresh;
		adev->pm.pm_display_cfg.crossfire_display_index = -1;
		adev->pm.pm_display_cfg.min_bus_bandwidth = 0;

		for (i = 0; i < pp_display_cfg->display_count; i++) {
			const struct dm_pp_single_disp_config *dc_cfg =
						&pp_display_cfg->disp_configs[i];
			adev->pm.pm_display_cfg.displays[i].controller_id = dc_cfg->pipe_idx + 1;
		}

		if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->display_configuration_change)
			adev->powerplay.pp_funcs->display_configuration_change(
				adev->powerplay.pp_handle,
				&adev->pm.pm_display_cfg);
		else
			smu_display_configuration_change(smu,
							 &adev->pm.pm_display_cfg);

		amdgpu_pm_compute_clocks(adev);
	}

	return true;
}

static void get_default_clock_levels(
		enum dm_pp_clock_type clk_type,
		struct dm_pp_clock_levels *clks)
{
	uint32_t disp_clks_in_khz[6] = {
			300000, 400000, 496560, 626090, 685720, 757900 };
	uint32_t sclks_in_khz[6] = {
			300000, 360000, 423530, 514290, 626090, 720000 };
	uint32_t mclks_in_khz[2] = { 333000, 800000 };

	switch (clk_type) {
	case DM_PP_CLOCK_TYPE_DISPLAY_CLK:
		clks->num_levels = 6;
		memmove(clks->clocks_in_khz, disp_clks_in_khz,
				sizeof(disp_clks_in_khz));
		break;
	case DM_PP_CLOCK_TYPE_ENGINE_CLK:
		clks->num_levels = 6;
		memmove(clks->clocks_in_khz, sclks_in_khz,
				sizeof(sclks_in_khz));
		break;
	case DM_PP_CLOCK_TYPE_MEMORY_CLK:
		clks->num_levels = 2;
		memmove(clks->clocks_in_khz, mclks_in_khz,
				sizeof(mclks_in_khz));
		break;
	default:
		clks->num_levels = 0;
		break;
	}
}

static enum smu_clk_type dc_to_smu_clock_type(
		enum dm_pp_clock_type dm_pp_clk_type)
{
	enum smu_clk_type smu_clk_type = SMU_CLK_COUNT;

	switch (dm_pp_clk_type) {
	case DM_PP_CLOCK_TYPE_DISPLAY_CLK:
		smu_clk_type = SMU_DISPCLK;
		break;
	case DM_PP_CLOCK_TYPE_ENGINE_CLK:
		smu_clk_type = SMU_GFXCLK;
		break;
	case DM_PP_CLOCK_TYPE_MEMORY_CLK:
		smu_clk_type = SMU_MCLK;
		break;
	case DM_PP_CLOCK_TYPE_DCEFCLK:
		smu_clk_type = SMU_DCEFCLK;
		break;
	case DM_PP_CLOCK_TYPE_SOCCLK:
		smu_clk_type = SMU_SOCCLK;
		break;
	default:
		DRM_ERROR("DM_PPLIB: invalid clock type: %d!\n",
			  dm_pp_clk_type);
		break;
	}

	return smu_clk_type;
}

static enum amd_pp_clock_type dc_to_pp_clock_type(
		enum dm_pp_clock_type dm_pp_clk_type)
{
	enum amd_pp_clock_type amd_pp_clk_type = 0;

	switch (dm_pp_clk_type) {
	case DM_PP_CLOCK_TYPE_DISPLAY_CLK:
		amd_pp_clk_type = amd_pp_disp_clock;
		break;
	case DM_PP_CLOCK_TYPE_ENGINE_CLK:
		amd_pp_clk_type = amd_pp_sys_clock;
		break;
	case DM_PP_CLOCK_TYPE_MEMORY_CLK:
		amd_pp_clk_type = amd_pp_mem_clock;
		break;
	case DM_PP_CLOCK_TYPE_DCEFCLK:
		amd_pp_clk_type  = amd_pp_dcef_clock;
		break;
	case DM_PP_CLOCK_TYPE_DCFCLK:
		amd_pp_clk_type = amd_pp_dcf_clock;
		break;
	case DM_PP_CLOCK_TYPE_PIXELCLK:
		amd_pp_clk_type = amd_pp_pixel_clock;
		break;
	case DM_PP_CLOCK_TYPE_FCLK:
		amd_pp_clk_type = amd_pp_f_clock;
		break;
	case DM_PP_CLOCK_TYPE_DISPLAYPHYCLK:
		amd_pp_clk_type = amd_pp_phy_clock;
		break;
	case DM_PP_CLOCK_TYPE_DPPCLK:
		amd_pp_clk_type = amd_pp_dpp_clock;
		break;
	default:
		DRM_ERROR("DM_PPLIB: invalid clock type: %d!\n",
				dm_pp_clk_type);
		break;
	}

	return amd_pp_clk_type;
}

static enum dm_pp_clocks_state pp_to_dc_powerlevel_state(
			enum PP_DAL_POWERLEVEL max_clocks_state)
{
	switch (max_clocks_state) {
	case PP_DAL_POWERLEVEL_0:
		return DM_PP_CLOCKS_DPM_STATE_LEVEL_0;
	case PP_DAL_POWERLEVEL_1:
		return DM_PP_CLOCKS_DPM_STATE_LEVEL_1;
	case PP_DAL_POWERLEVEL_2:
		return DM_PP_CLOCKS_DPM_STATE_LEVEL_2;
	case PP_DAL_POWERLEVEL_3:
		return DM_PP_CLOCKS_DPM_STATE_LEVEL_3;
	case PP_DAL_POWERLEVEL_4:
		return DM_PP_CLOCKS_DPM_STATE_LEVEL_4;
	case PP_DAL_POWERLEVEL_5:
		return DM_PP_CLOCKS_DPM_STATE_LEVEL_5;
	case PP_DAL_POWERLEVEL_6:
		return DM_PP_CLOCKS_DPM_STATE_LEVEL_6;
	case PP_DAL_POWERLEVEL_7:
		return DM_PP_CLOCKS_DPM_STATE_LEVEL_7;
	default:
		DRM_ERROR("DM_PPLIB: invalid powerlevel state: %d!\n",
				max_clocks_state);
		return DM_PP_CLOCKS_STATE_INVALID;
	}
}

static void pp_to_dc_clock_levels(
		const struct amd_pp_clocks *pp_clks,
		struct dm_pp_clock_levels *dc_clks,
		enum dm_pp_clock_type dc_clk_type)
{
	uint32_t i;

	if (pp_clks->count > DM_PP_MAX_CLOCK_LEVELS) {
		DRM_INFO("DM_PPLIB: Warning: %s clock: number of levels %d exceeds maximum of %d!\n",
				DC_DECODE_PP_CLOCK_TYPE(dc_clk_type),
				pp_clks->count,
				DM_PP_MAX_CLOCK_LEVELS);

		dc_clks->num_levels = DM_PP_MAX_CLOCK_LEVELS;
	} else
		dc_clks->num_levels = pp_clks->count;

	DRM_INFO("DM_PPLIB: values for %s clock\n",
			DC_DECODE_PP_CLOCK_TYPE(dc_clk_type));

	for (i = 0; i < dc_clks->num_levels; i++) {
		DRM_INFO("DM_PPLIB:\t %d\n", pp_clks->clock[i]);
		dc_clks->clocks_in_khz[i] = pp_clks->clock[i];
	}
}

static void pp_to_dc_clock_levels_with_latency(
		const struct pp_clock_levels_with_latency *pp_clks,
		struct dm_pp_clock_levels_with_latency *clk_level_info,
		enum dm_pp_clock_type dc_clk_type)
{
	uint32_t i;

	if (pp_clks->num_levels > DM_PP_MAX_CLOCK_LEVELS) {
		DRM_INFO("DM_PPLIB: Warning: %s clock: number of levels %d exceeds maximum of %d!\n",
				DC_DECODE_PP_CLOCK_TYPE(dc_clk_type),
				pp_clks->num_levels,
				DM_PP_MAX_CLOCK_LEVELS);

		clk_level_info->num_levels = DM_PP_MAX_CLOCK_LEVELS;
	} else
		clk_level_info->num_levels = pp_clks->num_levels;

	DRM_DEBUG("DM_PPLIB: values for %s clock\n",
			DC_DECODE_PP_CLOCK_TYPE(dc_clk_type));

	for (i = 0; i < clk_level_info->num_levels; i++) {
		DRM_DEBUG("DM_PPLIB:\t %d in kHz\n", pp_clks->data[i].clocks_in_khz);
		clk_level_info->data[i].clocks_in_khz = pp_clks->data[i].clocks_in_khz;
		clk_level_info->data[i].latency_in_us = pp_clks->data[i].latency_in_us;
	}
}

static void pp_to_dc_clock_levels_with_voltage(
		const struct pp_clock_levels_with_voltage *pp_clks,
		struct dm_pp_clock_levels_with_voltage *clk_level_info,
		enum dm_pp_clock_type dc_clk_type)
{
	uint32_t i;

	if (pp_clks->num_levels > DM_PP_MAX_CLOCK_LEVELS) {
		DRM_INFO("DM_PPLIB: Warning: %s clock: number of levels %d exceeds maximum of %d!\n",
				DC_DECODE_PP_CLOCK_TYPE(dc_clk_type),
				pp_clks->num_levels,
				DM_PP_MAX_CLOCK_LEVELS);

		clk_level_info->num_levels = DM_PP_MAX_CLOCK_LEVELS;
	} else
		clk_level_info->num_levels = pp_clks->num_levels;

	DRM_INFO("DM_PPLIB: values for %s clock\n",
			DC_DECODE_PP_CLOCK_TYPE(dc_clk_type));

	for (i = 0; i < clk_level_info->num_levels; i++) {
		DRM_INFO("DM_PPLIB:\t %d in kHz, %d in mV\n", pp_clks->data[i].clocks_in_khz,
			 pp_clks->data[i].voltage_in_mv);
		clk_level_info->data[i].clocks_in_khz = pp_clks->data[i].clocks_in_khz;
		clk_level_info->data[i].voltage_in_mv = pp_clks->data[i].voltage_in_mv;
	}
}

bool dm_pp_get_clock_levels_by_type(
		const struct dc_context *ctx,
		enum dm_pp_clock_type clk_type,
		struct dm_pp_clock_levels *dc_clks)
{
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	struct amd_pp_clocks pp_clks = { 0 };
	struct amd_pp_simple_clock_info validation_clks = { 0 };
	uint32_t i;

	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->get_clock_by_type) {
		if (adev->powerplay.pp_funcs->get_clock_by_type(pp_handle,
			dc_to_pp_clock_type(clk_type), &pp_clks)) {
		/* Error in pplib. Provide default values. */
			return true;
		}
	} else if (adev->smu.ppt_funcs && adev->smu.ppt_funcs->get_clock_by_type) {
		if (smu_get_clock_by_type(&adev->smu,
					  dc_to_pp_clock_type(clk_type),
					  &pp_clks)) {
			get_default_clock_levels(clk_type, dc_clks);
			return true;
		}
	}

	pp_to_dc_clock_levels(&pp_clks, dc_clks, clk_type);

	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->get_display_mode_validation_clocks) {
		if (adev->powerplay.pp_funcs->get_display_mode_validation_clocks(
						pp_handle, &validation_clks)) {
			/* Error in pplib. Provide default values. */
			DRM_INFO("DM_PPLIB: Warning: using default validation clocks!\n");
			validation_clks.engine_max_clock = 72000;
			validation_clks.memory_max_clock = 80000;
			validation_clks.level = 0;
		}
	} else if (adev->smu.ppt_funcs && adev->smu.ppt_funcs->get_max_high_clocks) {
		if (smu_get_max_high_clocks(&adev->smu, &validation_clks)) {
			DRM_INFO("DM_PPLIB: Warning: using default validation clocks!\n");
			validation_clks.engine_max_clock = 72000;
			validation_clks.memory_max_clock = 80000;
			validation_clks.level = 0;
		}
	}

	DRM_INFO("DM_PPLIB: Validation clocks:\n");
	DRM_INFO("DM_PPLIB:    engine_max_clock: %d\n",
			validation_clks.engine_max_clock);
	DRM_INFO("DM_PPLIB:    memory_max_clock: %d\n",
			validation_clks.memory_max_clock);
	DRM_INFO("DM_PPLIB:    level           : %d\n",
			validation_clks.level);

	/* Translate 10 kHz to kHz. */
	validation_clks.engine_max_clock *= 10;
	validation_clks.memory_max_clock *= 10;

	/* Determine the highest non-boosted level from the Validation Clocks */
	if (clk_type == DM_PP_CLOCK_TYPE_ENGINE_CLK) {
		for (i = 0; i < dc_clks->num_levels; i++) {
			if (dc_clks->clocks_in_khz[i] > validation_clks.engine_max_clock) {
				/* This clock is higher the validation clock.
				 * Than means the previous one is the highest
				 * non-boosted one. */
				DRM_INFO("DM_PPLIB: reducing engine clock level from %d to %d\n",
						dc_clks->num_levels, i);
				dc_clks->num_levels = i > 0 ? i : 1;
				break;
			}
		}
	} else if (clk_type == DM_PP_CLOCK_TYPE_MEMORY_CLK) {
		for (i = 0; i < dc_clks->num_levels; i++) {
			if (dc_clks->clocks_in_khz[i] > validation_clks.memory_max_clock) {
				DRM_INFO("DM_PPLIB: reducing memory clock level from %d to %d\n",
						dc_clks->num_levels, i);
				dc_clks->num_levels = i > 0 ? i : 1;
				break;
			}
		}
	}

	return true;
}

bool dm_pp_get_clock_levels_by_type_with_latency(
	const struct dc_context *ctx,
	enum dm_pp_clock_type clk_type,
	struct dm_pp_clock_levels_with_latency *clk_level_info)
{
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	struct pp_clock_levels_with_latency pp_clks = { 0 };
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret;

	if (pp_funcs && pp_funcs->get_clock_by_type_with_latency) {
		ret = pp_funcs->get_clock_by_type_with_latency(pp_handle,
						dc_to_pp_clock_type(clk_type),
						&pp_clks);
		if (ret)
			return false;
	} else if (adev->smu.ppt_funcs && adev->smu.ppt_funcs->get_clock_by_type_with_latency) {
		if (smu_get_clock_by_type_with_latency(&adev->smu,
						       dc_to_smu_clock_type(clk_type),
						       &pp_clks))
			return false;
	}


	pp_to_dc_clock_levels_with_latency(&pp_clks, clk_level_info, clk_type);

	return true;
}

bool dm_pp_get_clock_levels_by_type_with_voltage(
	const struct dc_context *ctx,
	enum dm_pp_clock_type clk_type,
	struct dm_pp_clock_levels_with_voltage *clk_level_info)
{
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	struct pp_clock_levels_with_voltage pp_clk_info = {0};
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	int ret;

	if (pp_funcs && pp_funcs->get_clock_by_type_with_voltage) {
		ret = pp_funcs->get_clock_by_type_with_voltage(pp_handle,
						dc_to_pp_clock_type(clk_type),
						&pp_clk_info);
		if (ret)
			return false;
	} else if (adev->smu.ppt_funcs && adev->smu.ppt_funcs->get_clock_by_type_with_voltage) {
		if (smu_get_clock_by_type_with_voltage(&adev->smu,
						       dc_to_pp_clock_type(clk_type),
						       &pp_clk_info))
			return false;
	}

	pp_to_dc_clock_levels_with_voltage(&pp_clk_info, clk_level_info, clk_type);

	return true;
}

bool dm_pp_notify_wm_clock_changes(
	const struct dc_context *ctx,
	struct dm_pp_wm_sets_with_clock_ranges *wm_with_clock_ranges)
{
	/* TODO: to be implemented */
	return false;
}

bool dm_pp_apply_power_level_change_request(
	const struct dc_context *ctx,
	struct dm_pp_power_level_change_request *level_change_req)
{
	/* TODO: to be implemented */
	return false;
}

bool dm_pp_apply_clock_for_voltage_request(
	const struct dc_context *ctx,
	struct dm_pp_clock_for_voltage_req *clock_for_voltage_req)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct pp_display_clock_request pp_clock_request = {0};
	int ret = 0;

	pp_clock_request.clock_type = dc_to_pp_clock_type(clock_for_voltage_req->clk_type);
	pp_clock_request.clock_freq_in_khz = clock_for_voltage_req->clocks_in_khz;

	if (!pp_clock_request.clock_type)
		return false;

	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->display_clock_voltage_request)
		ret = adev->powerplay.pp_funcs->display_clock_voltage_request(
			adev->powerplay.pp_handle,
			&pp_clock_request);
	else if (adev->smu.ppt_funcs &&
		 adev->smu.ppt_funcs->display_clock_voltage_request)
		ret = smu_display_clock_voltage_request(&adev->smu,
							&pp_clock_request);
	if (ret)
		return false;
	return true;
}

bool dm_pp_get_static_clocks(
	const struct dc_context *ctx,
	struct dm_pp_static_clock_info *static_clk_info)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct amd_pp_clock_info pp_clk_info = {0};
	int ret = 0;

	if (adev->powerplay.pp_funcs && adev->powerplay.pp_funcs->get_current_clocks)
		ret = adev->powerplay.pp_funcs->get_current_clocks(
			adev->powerplay.pp_handle,
			&pp_clk_info);
	else if (adev->smu.ppt_funcs)
		ret = smu_get_current_clocks(&adev->smu, &pp_clk_info);
	if (ret)
		return false;

	static_clk_info->max_clocks_state = pp_to_dc_powerlevel_state(pp_clk_info.max_clocks_state);
	static_clk_info->max_mclk_khz = pp_clk_info.max_memory_clock * 10;
	static_clk_info->max_sclk_khz = pp_clk_info.max_engine_clock * 10;

	return true;
}

void pp_rv_set_wm_ranges(struct pp_smu *pp,
		struct pp_smu_wm_range_sets *ranges)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;
	struct dm_pp_wm_sets_with_clock_ranges_soc15 wm_with_clock_ranges;
	struct dm_pp_clock_range_for_dmif_wm_set_soc15 *wm_dce_clocks = wm_with_clock_ranges.wm_dmif_clocks_ranges;
	struct dm_pp_clock_range_for_mcif_wm_set_soc15 *wm_soc_clocks = wm_with_clock_ranges.wm_mcif_clocks_ranges;
	int32_t i;

	wm_with_clock_ranges.num_wm_dmif_sets = ranges->num_reader_wm_sets;
	wm_with_clock_ranges.num_wm_mcif_sets = ranges->num_writer_wm_sets;

	for (i = 0; i < wm_with_clock_ranges.num_wm_dmif_sets; i++) {
		if (ranges->reader_wm_sets[i].wm_inst > 3)
			wm_dce_clocks[i].wm_set_id = WM_SET_A;
		else
			wm_dce_clocks[i].wm_set_id =
					ranges->reader_wm_sets[i].wm_inst;
		wm_dce_clocks[i].wm_max_dcfclk_clk_in_khz =
				ranges->reader_wm_sets[i].max_drain_clk_mhz * 1000;
		wm_dce_clocks[i].wm_min_dcfclk_clk_in_khz =
				ranges->reader_wm_sets[i].min_drain_clk_mhz * 1000;
		wm_dce_clocks[i].wm_max_mem_clk_in_khz =
				ranges->reader_wm_sets[i].max_fill_clk_mhz * 1000;
		wm_dce_clocks[i].wm_min_mem_clk_in_khz =
				ranges->reader_wm_sets[i].min_fill_clk_mhz * 1000;
	}

	for (i = 0; i < wm_with_clock_ranges.num_wm_mcif_sets; i++) {
		if (ranges->writer_wm_sets[i].wm_inst > 3)
			wm_soc_clocks[i].wm_set_id = WM_SET_A;
		else
			wm_soc_clocks[i].wm_set_id =
					ranges->writer_wm_sets[i].wm_inst;
		wm_soc_clocks[i].wm_max_socclk_clk_in_khz =
				ranges->writer_wm_sets[i].max_fill_clk_mhz * 1000;
		wm_soc_clocks[i].wm_min_socclk_clk_in_khz =
				ranges->writer_wm_sets[i].min_fill_clk_mhz * 1000;
		wm_soc_clocks[i].wm_max_mem_clk_in_khz =
				ranges->writer_wm_sets[i].max_drain_clk_mhz * 1000;
		wm_soc_clocks[i].wm_min_mem_clk_in_khz =
				ranges->writer_wm_sets[i].min_drain_clk_mhz * 1000;
	}

	if (pp_funcs && pp_funcs->set_watermarks_for_clocks_ranges)
		pp_funcs->set_watermarks_for_clocks_ranges(pp_handle,
							   &wm_with_clock_ranges);
	else
		smu_set_watermarks_for_clock_ranges(&adev->smu,
				&wm_with_clock_ranges);
}

void pp_rv_set_pme_wa_enable(struct pp_smu *pp)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (pp_funcs && pp_funcs->notify_smu_enable_pwe)
		pp_funcs->notify_smu_enable_pwe(pp_handle);
	else if (adev->smu.ppt_funcs)
		smu_notify_smu_enable_pwe(&adev->smu);
}

void pp_rv_set_active_display_count(struct pp_smu *pp, int count)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs || !pp_funcs->set_active_display_count)
		return;

	pp_funcs->set_active_display_count(pp_handle, count);
}

void pp_rv_set_min_deep_sleep_dcfclk(struct pp_smu *pp, int clock)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs || !pp_funcs->set_min_deep_sleep_dcefclk)
		return;

	pp_funcs->set_min_deep_sleep_dcefclk(pp_handle, clock);
}

void pp_rv_set_hard_min_dcefclk_by_freq(struct pp_smu *pp, int clock)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs || !pp_funcs->set_hard_min_dcefclk_by_freq)
		return;

	pp_funcs->set_hard_min_dcefclk_by_freq(pp_handle, clock);
}

void pp_rv_set_hard_min_fclk_by_freq(struct pp_smu *pp, int mhz)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	void *pp_handle = adev->powerplay.pp_handle;
	const struct amd_pm_funcs *pp_funcs = adev->powerplay.pp_funcs;

	if (!pp_funcs || !pp_funcs->set_hard_min_fclk_by_freq)
		return;

	pp_funcs->set_hard_min_fclk_by_freq(pp_handle, mhz);
}

enum pp_smu_status pp_nv_set_wm_ranges(struct pp_smu *pp,
		struct pp_smu_wm_range_sets *ranges)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct dm_pp_wm_sets_with_clock_ranges_soc15 wm_with_clock_ranges;
	struct dm_pp_clock_range_for_dmif_wm_set_soc15 *wm_dce_clocks =
			wm_with_clock_ranges.wm_dmif_clocks_ranges;
	struct dm_pp_clock_range_for_mcif_wm_set_soc15 *wm_soc_clocks =
			wm_with_clock_ranges.wm_mcif_clocks_ranges;
	int32_t i;

	wm_with_clock_ranges.num_wm_dmif_sets = ranges->num_reader_wm_sets;
	wm_with_clock_ranges.num_wm_mcif_sets = ranges->num_writer_wm_sets;

	for (i = 0; i < wm_with_clock_ranges.num_wm_dmif_sets; i++) {
		if (ranges->reader_wm_sets[i].wm_inst > 3)
			wm_dce_clocks[i].wm_set_id = WM_SET_A;
		else
			wm_dce_clocks[i].wm_set_id =
					ranges->reader_wm_sets[i].wm_inst;
		wm_dce_clocks[i].wm_max_dcfclk_clk_in_khz =
			ranges->reader_wm_sets[i].max_drain_clk_mhz * 1000;
		wm_dce_clocks[i].wm_min_dcfclk_clk_in_khz =
			ranges->reader_wm_sets[i].min_drain_clk_mhz * 1000;
		wm_dce_clocks[i].wm_max_mem_clk_in_khz =
			ranges->reader_wm_sets[i].max_fill_clk_mhz * 1000;
		wm_dce_clocks[i].wm_min_mem_clk_in_khz =
			ranges->reader_wm_sets[i].min_fill_clk_mhz * 1000;
	}

	for (i = 0; i < wm_with_clock_ranges.num_wm_mcif_sets; i++) {
		if (ranges->writer_wm_sets[i].wm_inst > 3)
			wm_soc_clocks[i].wm_set_id = WM_SET_A;
		else
			wm_soc_clocks[i].wm_set_id =
					ranges->writer_wm_sets[i].wm_inst;
		wm_soc_clocks[i].wm_max_socclk_clk_in_khz =
			ranges->writer_wm_sets[i].max_fill_clk_mhz * 1000;
		wm_soc_clocks[i].wm_min_socclk_clk_in_khz =
			ranges->writer_wm_sets[i].min_fill_clk_mhz * 1000;
		wm_soc_clocks[i].wm_max_mem_clk_in_khz =
			ranges->writer_wm_sets[i].max_drain_clk_mhz * 1000;
		wm_soc_clocks[i].wm_min_mem_clk_in_khz =
			ranges->writer_wm_sets[i].min_drain_clk_mhz * 1000;
	}

	smu_set_watermarks_for_clock_ranges(&adev->smu,	&wm_with_clock_ranges);

	return PP_SMU_RESULT_OK;
}

enum pp_smu_status pp_nv_set_pme_wa_enable(struct pp_smu *pp)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	/* 0: successful or smu.ppt_funcs->set_azalia_d3_pme = NULL;  1: fail */
	if (smu_set_azalia_d3_pme(smu))
		return PP_SMU_RESULT_FAIL;

	return PP_SMU_RESULT_OK;
}

enum pp_smu_status pp_nv_set_display_count(struct pp_smu *pp, int count)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	/* 0: successful or smu.ppt_funcs->set_display_count = NULL;  1: fail */
	if (smu_set_display_count(smu, count))
		return PP_SMU_RESULT_FAIL;

	return PP_SMU_RESULT_OK;
}

enum pp_smu_status pp_nv_set_min_deep_sleep_dcfclk(struct pp_smu *pp, int mhz)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	/* 0: successful or smu.ppt_funcs->set_deep_sleep_dcefclk = NULL;1: fail */
	if (smu_set_deep_sleep_dcefclk(smu, mhz))
		return PP_SMU_RESULT_FAIL;

	return PP_SMU_RESULT_OK;
}

enum pp_smu_status pp_nv_set_hard_min_dcefclk_by_freq(
		struct pp_smu *pp, int mhz)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;
	struct pp_display_clock_request clock_req;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	clock_req.clock_type = amd_pp_dcef_clock;
	clock_req.clock_freq_in_khz = mhz * 1000;

	/* 0: successful or smu.ppt_funcs->display_clock_voltage_request = NULL
	 * 1: fail
	 */
	if (smu_display_clock_voltage_request(smu, &clock_req))
		return PP_SMU_RESULT_FAIL;

	return PP_SMU_RESULT_OK;
}

enum pp_smu_status pp_nv_set_hard_min_uclk_by_freq(struct pp_smu *pp, int mhz)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;
	struct pp_display_clock_request clock_req;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	clock_req.clock_type = amd_pp_mem_clock;
	clock_req.clock_freq_in_khz = mhz * 1000;

	/* 0: successful or smu.ppt_funcs->display_clock_voltage_request = NULL
	 * 1: fail
	 */
	if (smu_display_clock_voltage_request(smu, &clock_req))
		return PP_SMU_RESULT_FAIL;

	return PP_SMU_RESULT_OK;
}

enum pp_smu_status pp_nv_set_pstate_handshake_support(
	struct pp_smu *pp, BOOLEAN pstate_handshake_supported)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;

	if (smu_display_disable_memory_clock_switch(smu, !pstate_handshake_supported))
		return PP_SMU_RESULT_FAIL;

	return PP_SMU_RESULT_OK;
}

enum pp_smu_status pp_nv_set_voltage_by_freq(struct pp_smu *pp,
		enum pp_smu_nv_clock_id clock_id, int mhz)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;
	struct pp_display_clock_request clock_req;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	switch (clock_id) {
	case PP_SMU_NV_DISPCLK:
		clock_req.clock_type = amd_pp_disp_clock;
		break;
	case PP_SMU_NV_PHYCLK:
		clock_req.clock_type = amd_pp_phy_clock;
		break;
	case PP_SMU_NV_PIXELCLK:
		clock_req.clock_type = amd_pp_pixel_clock;
		break;
	default:
		break;
	}
	clock_req.clock_freq_in_khz = mhz * 1000;

	/* 0: successful or smu.ppt_funcs->display_clock_voltage_request = NULL
	 * 1: fail
	 */
	if (smu_display_clock_voltage_request(smu, &clock_req))
		return PP_SMU_RESULT_FAIL;

	return PP_SMU_RESULT_OK;
}

enum pp_smu_status pp_nv_get_maximum_sustainable_clocks(
		struct pp_smu *pp, struct pp_smu_nv_clock_table *max_clocks)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	if (!smu->ppt_funcs->get_max_sustainable_clocks_by_dc)
		return PP_SMU_RESULT_UNSUPPORTED;

	if (!smu_get_max_sustainable_clocks_by_dc(smu, max_clocks))
		return PP_SMU_RESULT_OK;

	return PP_SMU_RESULT_FAIL;
}

enum pp_smu_status pp_nv_get_uclk_dpm_states(struct pp_smu *pp,
		unsigned int *clock_values_in_khz, unsigned int *num_states)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	if (!smu->ppt_funcs->get_uclk_dpm_states)
		return PP_SMU_RESULT_UNSUPPORTED;

	if (!smu_get_uclk_dpm_states(smu,
			clock_values_in_khz, num_states))
		return PP_SMU_RESULT_OK;

	return PP_SMU_RESULT_FAIL;
}

#ifdef CONFIG_DRM_AMD_DC_DCN2_1
enum pp_smu_status pp_rn_get_dpm_clock_table(
		struct pp_smu *pp, struct dpm_clocks *clock_table)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	if (!smu->ppt_funcs->get_dpm_clock_table)
		return PP_SMU_RESULT_UNSUPPORTED;

	if (!smu_get_dpm_clock_table(smu, clock_table))
		return PP_SMU_RESULT_OK;

	return PP_SMU_RESULT_FAIL;
}

enum pp_smu_status pp_rn_set_wm_ranges(struct pp_smu *pp,
		struct pp_smu_wm_range_sets *ranges)
{
	const struct dc_context *ctx = pp->dm;
	struct amdgpu_device *adev = ctx->driver_context;
	struct smu_context *smu = &adev->smu;
	struct dm_pp_wm_sets_with_clock_ranges_soc15 wm_with_clock_ranges;
	struct dm_pp_clock_range_for_dmif_wm_set_soc15 *wm_dce_clocks =
			wm_with_clock_ranges.wm_dmif_clocks_ranges;
	struct dm_pp_clock_range_for_mcif_wm_set_soc15 *wm_soc_clocks =
			wm_with_clock_ranges.wm_mcif_clocks_ranges;
	int32_t i;

	if (!smu->ppt_funcs)
		return PP_SMU_RESULT_UNSUPPORTED;

	wm_with_clock_ranges.num_wm_dmif_sets = ranges->num_reader_wm_sets;
	wm_with_clock_ranges.num_wm_mcif_sets = ranges->num_writer_wm_sets;

	for (i = 0; i < wm_with_clock_ranges.num_wm_dmif_sets; i++) {
		if (ranges->reader_wm_sets[i].wm_inst > 3)
			wm_dce_clocks[i].wm_set_id = WM_SET_A;
		else
			wm_dce_clocks[i].wm_set_id =
					ranges->reader_wm_sets[i].wm_inst;

		wm_dce_clocks[i].wm_min_dcfclk_clk_in_khz =
			ranges->reader_wm_sets[i].min_drain_clk_mhz;

		wm_dce_clocks[i].wm_max_dcfclk_clk_in_khz =
			ranges->reader_wm_sets[i].max_drain_clk_mhz;

		wm_dce_clocks[i].wm_min_mem_clk_in_khz =
			ranges->reader_wm_sets[i].min_fill_clk_mhz;

		wm_dce_clocks[i].wm_max_mem_clk_in_khz =
			ranges->reader_wm_sets[i].max_fill_clk_mhz;
	}

	for (i = 0; i < wm_with_clock_ranges.num_wm_mcif_sets; i++) {
		if (ranges->writer_wm_sets[i].wm_inst > 3)
			wm_soc_clocks[i].wm_set_id = WM_SET_A;
		else
			wm_soc_clocks[i].wm_set_id =
					ranges->writer_wm_sets[i].wm_inst;
		wm_soc_clocks[i].wm_min_socclk_clk_in_khz =
				ranges->writer_wm_sets[i].min_fill_clk_mhz;

		wm_soc_clocks[i].wm_max_socclk_clk_in_khz =
			ranges->writer_wm_sets[i].max_fill_clk_mhz;

		wm_soc_clocks[i].wm_min_mem_clk_in_khz =
			ranges->writer_wm_sets[i].min_drain_clk_mhz;

		wm_soc_clocks[i].wm_max_mem_clk_in_khz =
			ranges->writer_wm_sets[i].max_drain_clk_mhz;
	}

	smu_set_watermarks_for_clock_ranges(&adev->smu, &wm_with_clock_ranges);

	return PP_SMU_RESULT_OK;
}
#endif

void dm_pp_get_funcs(
		struct dc_context *ctx,
		struct pp_smu_funcs *funcs)
{
	switch (ctx->dce_version) {
	case DCN_VERSION_1_0:
	case DCN_VERSION_1_01:
		funcs->ctx.ver = PP_SMU_VER_RV;
		funcs->rv_funcs.pp_smu.dm = ctx;
		funcs->rv_funcs.set_wm_ranges = pp_rv_set_wm_ranges;
		funcs->rv_funcs.set_pme_wa_enable = pp_rv_set_pme_wa_enable;
		funcs->rv_funcs.set_display_count =
				pp_rv_set_active_display_count;
		funcs->rv_funcs.set_min_deep_sleep_dcfclk =
				pp_rv_set_min_deep_sleep_dcfclk;
		funcs->rv_funcs.set_hard_min_dcfclk_by_freq =
				pp_rv_set_hard_min_dcefclk_by_freq;
		funcs->rv_funcs.set_hard_min_fclk_by_freq =
				pp_rv_set_hard_min_fclk_by_freq;
		break;
#ifdef CONFIG_DRM_AMD_DC_DCN2_0
	case DCN_VERSION_2_0:
		funcs->ctx.ver = PP_SMU_VER_NV;
		funcs->nv_funcs.pp_smu.dm = ctx;
		funcs->nv_funcs.set_display_count = pp_nv_set_display_count;
		funcs->nv_funcs.set_hard_min_dcfclk_by_freq =
				pp_nv_set_hard_min_dcefclk_by_freq;
		funcs->nv_funcs.set_min_deep_sleep_dcfclk =
				pp_nv_set_min_deep_sleep_dcfclk;
		funcs->nv_funcs.set_voltage_by_freq =
				pp_nv_set_voltage_by_freq;
		funcs->nv_funcs.set_wm_ranges = pp_nv_set_wm_ranges;

		/* todo set_pme_wa_enable cause 4k@6ohz display not light up */
		funcs->nv_funcs.set_pme_wa_enable = NULL;
		/* todo debug waring message */
		funcs->nv_funcs.set_hard_min_uclk_by_freq = pp_nv_set_hard_min_uclk_by_freq;
		/* todo  compare data with window driver*/
		funcs->nv_funcs.get_maximum_sustainable_clocks = pp_nv_get_maximum_sustainable_clocks;
		/*todo  compare data with window driver */
		funcs->nv_funcs.get_uclk_dpm_states = pp_nv_get_uclk_dpm_states;
		funcs->nv_funcs.set_pstate_handshake_support = pp_nv_set_pstate_handshake_support;
		break;
#endif

#ifdef CONFIG_DRM_AMD_DC_DCN2_1
	case DCN_VERSION_2_1:
		funcs->ctx.ver = PP_SMU_VER_RN;
		funcs->rn_funcs.pp_smu.dm = ctx;
		funcs->rn_funcs.set_wm_ranges = pp_rn_set_wm_ranges;
		funcs->rn_funcs.get_dpm_clock_table = pp_rn_get_dpm_clock_table;
		break;
#endif
	default:
		DRM_ERROR("smu version is not supported !\n");
		break;
	}
}
