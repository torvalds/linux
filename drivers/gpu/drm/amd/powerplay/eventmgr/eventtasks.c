/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

#include "eventmgr.h"
#include "eventinit.h"
#include "eventmanagement.h"
#include "eventmanager.h"
#include "hardwaremanager.h"
#include "eventtasks.h"
#include "power_state.h"
#include "hwmgr.h"
#include "amd_powerplay.h"
#include "psm.h"

#define TEMP_RANGE_MIN (90 * 1000)
#define TEMP_RANGE_MAX (120 * 1000)

int pem_task_update_allowed_performance_levels(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{

	if (eventmgr == NULL || eventmgr->hwmgr == NULL)
		return -EINVAL;

	if (pem_is_hw_access_blocked(eventmgr))
		return 0;

	phm_force_dpm_levels(eventmgr->hwmgr, eventmgr->hwmgr->dpm_level);

	return 0;
}

/* eventtasks_generic.c */
int pem_task_adjust_power_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	struct pp_hwmgr *hwmgr;

	if (pem_is_hw_access_blocked(eventmgr))
		return 0;

	hwmgr = eventmgr->hwmgr;
	if (event_data->pnew_power_state != NULL)
		hwmgr->request_ps = event_data->pnew_power_state;

	if (phm_cap_enabled(eventmgr->platform_descriptor->platformCaps, PHM_PlatformCaps_DynamicPatchPowerState))
		psm_adjust_power_state_dynamic(eventmgr, event_data->skip_state_adjust_rules);
	else
		psm_adjust_power_state_static(eventmgr, event_data->skip_state_adjust_rules);

	return 0;
}

int pem_task_power_down_asic(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_power_down_asic(eventmgr->hwmgr);
}

int pem_task_set_boot_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	if (pem_is_event_data_valid(event_data->valid_fields, PEM_EventDataValid_RequestedStateID))
		return psm_set_states(eventmgr, &(event_data->requested_state_id));

	return 0;
}

int pem_task_reset_boot_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_update_new_power_state_clocks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_system_shutdown(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_register_interrupts(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_unregister_interrupts(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return pem_unregister_interrupts(eventmgr);
}

int pem_task_get_boot_state_id(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	int result;

	result = psm_get_state_by_classification(eventmgr,
		PP_StateClassificationFlag_Boot,
		&(event_data->requested_state_id)
	);

	if (0 == result)
		pem_set_event_data_valid(event_data->valid_fields, PEM_EventDataValid_RequestedStateID);
	else
		pem_unset_event_data_valid(event_data->valid_fields, PEM_EventDataValid_RequestedStateID);

	return result;
}

int pem_task_enable_dynamic_state_management(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_enable_dynamic_state_management(eventmgr->hwmgr);
}

int pem_task_disable_dynamic_state_management(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_disable_dynamic_state_management(eventmgr->hwmgr);
}

int pem_task_enable_clock_power_gatings_tasks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_enable_clock_power_gatings(eventmgr->hwmgr);
}

int pem_task_powerdown_uvd_tasks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_powerdown_uvd(eventmgr->hwmgr);
}

int pem_task_powerdown_vce_tasks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	phm_powergate_uvd(eventmgr->hwmgr, true);
	phm_powergate_vce(eventmgr->hwmgr, true);
	return 0;
}

int pem_task_disable_clock_power_gatings_tasks(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	phm_disable_clock_power_gatings(eventmgr->hwmgr);
	return 0;
}

int pem_task_start_asic_block_usage(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_stop_asic_block_usage(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_disable_smc_firmware_ctf(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_disable_smc_firmware_ctf(eventmgr->hwmgr);
}

int pem_task_setup_asic(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_setup_asic(eventmgr->hwmgr);
}

int pem_task_cleanup_asic(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_store_dal_configuration(struct pp_eventmgr *eventmgr, const struct amd_display_configuration *display_config)
{
	/* TODO */
	return 0;
	/*phm_store_dal_configuration_data(eventmgr->hwmgr, display_config) */
}

int pem_task_notify_hw_mgr_display_configuration_change(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	if (pem_is_hw_access_blocked(eventmgr))
		return 0;

	return phm_display_configuration_changed(eventmgr->hwmgr);
}

int pem_task_notify_hw_mgr_pre_display_configuration_change(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return 0;
}

int pem_task_notify_smc_display_config_after_power_state_adjustment(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	if (pem_is_hw_access_blocked(eventmgr))
		return 0;

	return phm_notify_smc_display_config_after_ps_adjustment(eventmgr->hwmgr);
}

int pem_task_block_adjust_power_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	eventmgr->block_adjust_power_state = true;
	/* to do PHM_ResetIPSCounter(pEventMgr->pHwMgr);*/
	return 0;
}

int pem_task_unblock_adjust_power_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	eventmgr->block_adjust_power_state = false;
	return 0;
}

int pem_task_notify_power_state_change(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_block_hw_access(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_un_block_hw_access(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_reset_display_phys_access(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_set_cpu_power_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_set_cpu_power_state(eventmgr->hwmgr);
}

/*powersaving*/

int pem_task_set_power_source(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_notify_hw_of_power_source(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_get_power_saving_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_reset_power_saving_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_set_power_saving_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_set_screen_state_on(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_set_screen_state_off(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_enable_voltage_island_power_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_disable_voltage_island_power_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_enable_cgpg(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_disable_cgpg(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_enable_clock_power_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}


int pem_task_enable_gfx_clock_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_disable_gfx_clock_gating(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}


/* performance */
int pem_task_set_performance_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	if (pem_is_event_data_valid(event_data->valid_fields, PEM_EventDataValid_RequestedStateID))
		return psm_set_states(eventmgr, &(event_data->requested_state_id));

	return 0;
}

int pem_task_conditionally_force_3d_performance_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_enable_stutter_mode(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	/* TODO */
	return 0;
}

int pem_task_get_2D_performance_state_id(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	int result;

	if (eventmgr->features[PP_Feature_PowerPlay].supported &&
		!(eventmgr->features[PP_Feature_PowerPlay].enabled))
			result = psm_get_state_by_classification(eventmgr,
					PP_StateClassificationFlag_Boot,
					&(event_data->requested_state_id));
	else if (eventmgr->features[PP_Feature_User2DPerformance].enabled)
			result = psm_get_state_by_classification(eventmgr,
				   PP_StateClassificationFlag_User2DPerformance,
					&(event_data->requested_state_id));
	else
		result = psm_get_ui_state(eventmgr, PP_StateUILabel_Performance,
					&(event_data->requested_state_id));

	if (0 == result)
		pem_set_event_data_valid(event_data->valid_fields, PEM_EventDataValid_RequestedStateID);
	else
		pem_unset_event_data_valid(event_data->valid_fields, PEM_EventDataValid_RequestedStateID);

	return result;
}

int pem_task_create_user_performance_state(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	struct pp_power_state *state;
	int table_entries;
	struct pp_hwmgr *hwmgr = eventmgr->hwmgr;
	int i;

	table_entries = hwmgr->num_ps;
	state = hwmgr->ps;

restart_search:
	for (i = 0; i < table_entries; i++) {
		if (state->classification.ui_label & event_data->requested_ui_label) {
			event_data->pnew_power_state = state;
			return 0;
		}
		state = (struct pp_power_state *)((unsigned long)state + hwmgr->ps_size);
	}

	switch (event_data->requested_ui_label) {
	case PP_StateUILabel_Battery:
	case PP_StateUILabel_Balanced:
		event_data->requested_ui_label = PP_StateUILabel_Performance;
		goto restart_search;
	default:
		break;
	}
	return -1;
}

int pem_task_initialize_thermal_controller(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	struct PP_TemperatureRange range;

	range.max = TEMP_RANGE_MAX;
	range.min = TEMP_RANGE_MIN;

	if (eventmgr == NULL || eventmgr->platform_descriptor == NULL)
		return -EINVAL;

	if (phm_cap_enabled(eventmgr->platform_descriptor->platformCaps, PHM_PlatformCaps_ThermalController))
		return phm_start_thermal_controller(eventmgr->hwmgr, &range);

	return 0;
}

int pem_task_uninitialize_thermal_controller(struct pp_eventmgr *eventmgr, struct pem_event_data *event_data)
{
	return phm_stop_thermal_controller(eventmgr->hwmgr);
}
