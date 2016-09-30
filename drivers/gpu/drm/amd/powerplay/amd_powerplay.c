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
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include "amd_shared.h"
#include "amd_powerplay.h"
#include "pp_instance.h"
#include "power_state.h"
#include "eventmanager.h"
#include "pp_debug.h"


#define PP_CHECK(handle)						\
	do {								\
		if ((handle) == NULL || (handle)->pp_valid != PP_VALID)	\
			return -EINVAL;					\
	} while (0)

#define PP_CHECK_HW(hwmgr)						\
	do {								\
		if ((hwmgr) == NULL || (hwmgr)->hwmgr_func == NULL)	\
			return -EINVAL;					\
	} while (0)

static int pp_early_init(void *handle)
{
	return 0;
}

static int pp_sw_init(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_hwmgr  *hwmgr;
	int ret = 0;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	hwmgr = pp_handle->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->pptable_func == NULL ||
	    hwmgr->pptable_func->pptable_init == NULL ||
	    hwmgr->hwmgr_func->backend_init == NULL)
		return -EINVAL;

	ret = hwmgr->pptable_func->pptable_init(hwmgr);
	if (ret)
		goto err;

	ret = hwmgr->hwmgr_func->backend_init(hwmgr);
	if (ret)
		goto err1;

	pr_info("amdgpu: powerplay initialized\n");

	return 0;
err1:
	if (hwmgr->pptable_func->pptable_fini)
		hwmgr->pptable_func->pptable_fini(hwmgr);
err:
	pr_err("amdgpu: powerplay initialization failed\n");
	return ret;
}

static int pp_sw_fini(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_hwmgr  *hwmgr;
	int ret = 0;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	hwmgr = pp_handle->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->backend_fini != NULL)
		ret = hwmgr->hwmgr_func->backend_fini(hwmgr);

	if (hwmgr->pptable_func->pptable_fini)
		hwmgr->pptable_func->pptable_fini(hwmgr);

	return ret;
}

static int pp_hw_init(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_smumgr *smumgr;
	struct pp_eventmgr *eventmgr;
	int ret = 0;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	smumgr = pp_handle->smu_mgr;

	if (smumgr == NULL || smumgr->smumgr_funcs == NULL ||
		smumgr->smumgr_funcs->smu_init == NULL ||
		smumgr->smumgr_funcs->start_smu == NULL)
		return -EINVAL;

	ret = smumgr->smumgr_funcs->smu_init(smumgr);
	if (ret) {
		printk(KERN_ERR "[ powerplay ] smc initialization failed\n");
		return ret;
	}

	ret = smumgr->smumgr_funcs->start_smu(smumgr);
	if (ret) {
		printk(KERN_ERR "[ powerplay ] smc start failed\n");
		smumgr->smumgr_funcs->smu_fini(smumgr);
		return ret;
	}

	hw_init_power_state_table(pp_handle->hwmgr);
	eventmgr = pp_handle->eventmgr;

	if (eventmgr == NULL || eventmgr->pp_eventmgr_init == NULL)
		return -EINVAL;

	ret = eventmgr->pp_eventmgr_init(eventmgr);
	return 0;
}

static int pp_hw_fini(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_smumgr *smumgr;
	struct pp_eventmgr *eventmgr;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	eventmgr = pp_handle->eventmgr;

	if (eventmgr != NULL && eventmgr->pp_eventmgr_fini != NULL)
		eventmgr->pp_eventmgr_fini(eventmgr);

	smumgr = pp_handle->smu_mgr;

	if (smumgr != NULL && smumgr->smumgr_funcs != NULL &&
		smumgr->smumgr_funcs->smu_fini != NULL)
		smumgr->smumgr_funcs->smu_fini(smumgr);

	return 0;
}

static bool pp_is_idle(void *handle)
{
	return false;
}

static int pp_wait_for_idle(void *handle)
{
	return 0;
}

static int pp_sw_reset(void *handle)
{
	return 0;
}


int amd_set_clockgating_by_smu(void *handle, uint32_t msg_id)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->update_clock_gatings == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->update_clock_gatings(hwmgr, &msg_id);
}

static int pp_set_powergating_state(void *handle,
				    enum amd_powergating_state state)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->enable_per_cu_power_gating == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	/* Enable/disable GFX per cu powergating through SMU */
	return hwmgr->hwmgr_func->enable_per_cu_power_gating(hwmgr,
			state == AMD_PG_STATE_GATE ? true : false);
}

static int pp_suspend(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_eventmgr *eventmgr;
	struct pem_event_data event_data = { {0} };

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	eventmgr = pp_handle->eventmgr;
	pem_handle_event(eventmgr, AMD_PP_EVENT_SUSPEND, &event_data);
	return 0;
}

static int pp_resume(void *handle)
{
	struct pp_instance *pp_handle;
	struct pp_eventmgr *eventmgr;
	struct pem_event_data event_data = { {0} };
	struct pp_smumgr *smumgr;
	int ret;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;
	smumgr = pp_handle->smu_mgr;

	if (smumgr == NULL || smumgr->smumgr_funcs == NULL ||
		smumgr->smumgr_funcs->start_smu == NULL)
		return -EINVAL;

	ret = smumgr->smumgr_funcs->start_smu(smumgr);
	if (ret) {
		printk(KERN_ERR "[ powerplay ] smc start failed\n");
		smumgr->smumgr_funcs->smu_fini(smumgr);
		return ret;
	}

	eventmgr = pp_handle->eventmgr;
	pem_handle_event(eventmgr, AMD_PP_EVENT_RESUME, &event_data);

	return 0;
}

const struct amd_ip_funcs pp_ip_funcs = {
	.name = "powerplay",
	.early_init = pp_early_init,
	.late_init = NULL,
	.sw_init = pp_sw_init,
	.sw_fini = pp_sw_fini,
	.hw_init = pp_hw_init,
	.hw_fini = pp_hw_fini,
	.suspend = pp_suspend,
	.resume = pp_resume,
	.is_idle = pp_is_idle,
	.wait_for_idle = pp_wait_for_idle,
	.soft_reset = pp_sw_reset,
	.set_clockgating_state = NULL,
	.set_powergating_state = pp_set_powergating_state,
};

static int pp_dpm_load_fw(void *handle)
{
	return 0;
}

static int pp_dpm_fw_loading_complete(void *handle)
{
	return 0;
}

static int pp_dpm_force_performance_level(void *handle,
					enum amd_dpm_forced_level level)
{
	struct pp_instance *pp_handle;
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	pp_handle = (struct pp_instance *)handle;

	hwmgr = pp_handle->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->force_dpm_level == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	hwmgr->hwmgr_func->force_dpm_level(hwmgr, level);

	return 0;
}

static enum amd_dpm_forced_level pp_dpm_get_performance_level(
								void *handle)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	if (hwmgr == NULL)
		return -EINVAL;

	return (((struct pp_instance *)handle)->hwmgr->dpm_level);
}

static int pp_dpm_get_sclk(void *handle, bool low)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->get_sclk == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->get_sclk(hwmgr, low);
}

static int pp_dpm_get_mclk(void *handle, bool low)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->get_mclk == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->get_mclk(hwmgr, low);
}

static int pp_dpm_powergate_vce(void *handle, bool gate)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->powergate_vce == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->powergate_vce(hwmgr, gate);
}

static int pp_dpm_powergate_uvd(void *handle, bool gate)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->powergate_uvd == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->powergate_uvd(hwmgr, gate);
}

static enum PP_StateUILabel power_state_convert(enum amd_pm_state_type  state)
{
	switch (state) {
	case POWER_STATE_TYPE_BATTERY:
		return PP_StateUILabel_Battery;
	case POWER_STATE_TYPE_BALANCED:
		return PP_StateUILabel_Balanced;
	case POWER_STATE_TYPE_PERFORMANCE:
		return PP_StateUILabel_Performance;
	default:
		return PP_StateUILabel_None;
	}
}

static int pp_dpm_dispatch_tasks(void *handle, enum amd_pp_event event_id,
		void *input, void *output)
{
	int ret = 0;
	struct pp_instance *pp_handle;
	struct pem_event_data data = { {0} };

	pp_handle = (struct pp_instance *)handle;

	if (pp_handle == NULL)
		return -EINVAL;

	switch (event_id) {
	case AMD_PP_EVENT_DISPLAY_CONFIG_CHANGE:
		ret = pem_handle_event(pp_handle->eventmgr, event_id, &data);
		break;
	case AMD_PP_EVENT_ENABLE_USER_STATE:
	{
		enum amd_pm_state_type  ps;

		if (input == NULL)
			return -EINVAL;
		ps = *(unsigned long *)input;

		data.requested_ui_label = power_state_convert(ps);
		ret = pem_handle_event(pp_handle->eventmgr, event_id, &data);
		break;
	}
	case AMD_PP_EVENT_COMPLETE_INIT:
		ret = pem_handle_event(pp_handle->eventmgr, event_id, &data);
		break;
	case AMD_PP_EVENT_READJUST_POWER_STATE:
		ret = pem_handle_event(pp_handle->eventmgr, event_id, &data);
		break;
	default:
		break;
	}
	return ret;
}

static enum amd_pm_state_type pp_dpm_get_current_power_state(void *handle)
{
	struct pp_hwmgr *hwmgr;
	struct pp_power_state *state;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	if (hwmgr == NULL || hwmgr->current_ps == NULL)
		return -EINVAL;

	state = hwmgr->current_ps;

	switch (state->classification.ui_label) {
	case PP_StateUILabel_Battery:
		return POWER_STATE_TYPE_BATTERY;
	case PP_StateUILabel_Balanced:
		return POWER_STATE_TYPE_BALANCED;
	case PP_StateUILabel_Performance:
		return POWER_STATE_TYPE_PERFORMANCE;
	default:
		if (state->classification.flags & PP_StateClassificationFlag_Boot)
			return  POWER_STATE_TYPE_INTERNAL_BOOT;
		else
			return POWER_STATE_TYPE_DEFAULT;
	}
}

static int pp_dpm_set_fan_control_mode(void *handle, uint32_t mode)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->set_fan_control_mode == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->set_fan_control_mode(hwmgr, mode);
}

static int pp_dpm_get_fan_control_mode(void *handle)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->get_fan_control_mode == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->get_fan_control_mode(hwmgr);
}

static int pp_dpm_set_fan_speed_percent(void *handle, uint32_t percent)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->set_fan_speed_percent == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->set_fan_speed_percent(hwmgr, percent);
}

static int pp_dpm_get_fan_speed_percent(void *handle, uint32_t *speed)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->get_fan_speed_percent == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->get_fan_speed_percent(hwmgr, speed);
}

static int pp_dpm_get_temperature(void *handle)
{
	struct pp_hwmgr  *hwmgr;

	if (handle == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->get_temperature == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->get_temperature(hwmgr);
}

static int pp_dpm_get_pp_num_states(void *handle,
		struct pp_states_info *data)
{
	struct pp_hwmgr *hwmgr;
	int i;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	if (hwmgr == NULL || hwmgr->ps == NULL)
		return -EINVAL;

	data->nums = hwmgr->num_ps;

	for (i = 0; i < hwmgr->num_ps; i++) {
		struct pp_power_state *state = (struct pp_power_state *)
				((unsigned long)hwmgr->ps + i * hwmgr->ps_size);
		switch (state->classification.ui_label) {
		case PP_StateUILabel_Battery:
			data->states[i] = POWER_STATE_TYPE_BATTERY;
			break;
		case PP_StateUILabel_Balanced:
			data->states[i] = POWER_STATE_TYPE_BALANCED;
			break;
		case PP_StateUILabel_Performance:
			data->states[i] = POWER_STATE_TYPE_PERFORMANCE;
			break;
		default:
			if (state->classification.flags & PP_StateClassificationFlag_Boot)
				data->states[i] = POWER_STATE_TYPE_INTERNAL_BOOT;
			else
				data->states[i] = POWER_STATE_TYPE_DEFAULT;
		}
	}

	return 0;
}

static int pp_dpm_get_pp_table(void *handle, char **table)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (!hwmgr->soft_pp_table)
		return -EINVAL;

	*table = (char *)hwmgr->soft_pp_table;

	return hwmgr->soft_pp_table_size;
}

static int pp_dpm_set_pp_table(void *handle, const char *buf, size_t size)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (!hwmgr->hardcode_pp_table) {
		hwmgr->hardcode_pp_table = kmemdup(hwmgr->soft_pp_table,
						   hwmgr->soft_pp_table_size,
						   GFP_KERNEL);

		if (!hwmgr->hardcode_pp_table)
			return -ENOMEM;
	}

	memcpy(hwmgr->hardcode_pp_table, buf, size);

	hwmgr->soft_pp_table = hwmgr->hardcode_pp_table;

	return amd_powerplay_reset(handle);
}

static int pp_dpm_force_clock_level(void *handle,
		enum pp_clock_type type, uint32_t mask)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->force_clock_level == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->force_clock_level(hwmgr, type, mask);
}

static int pp_dpm_print_clock_levels(void *handle,
		enum pp_clock_type type, char *buf)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->print_clock_levels == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}
	return hwmgr->hwmgr_func->print_clock_levels(hwmgr, type, buf);
}

static int pp_dpm_get_sclk_od(void *handle)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->get_sclk_od == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->get_sclk_od(hwmgr);
}

static int pp_dpm_set_sclk_od(void *handle, uint32_t value)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->set_sclk_od == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->set_sclk_od(hwmgr, value);
}

static int pp_dpm_get_mclk_od(void *handle)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->get_mclk_od == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->get_mclk_od(hwmgr);
}

static int pp_dpm_set_mclk_od(void *handle, uint32_t value)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->set_mclk_od == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->set_mclk_od(hwmgr, value);
}

static int pp_dpm_read_sensor(void *handle, int idx, int32_t *value)
{
	struct pp_hwmgr *hwmgr;

	if (!handle)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	PP_CHECK_HW(hwmgr);

	if (hwmgr->hwmgr_func->read_sensor == NULL) {
		printk(KERN_INFO "%s was not implemented.\n", __func__);
		return 0;
	}

	return hwmgr->hwmgr_func->read_sensor(hwmgr, idx, value);
}

const struct amd_powerplay_funcs pp_dpm_funcs = {
	.get_temperature = pp_dpm_get_temperature,
	.load_firmware = pp_dpm_load_fw,
	.wait_for_fw_loading_complete = pp_dpm_fw_loading_complete,
	.force_performance_level = pp_dpm_force_performance_level,
	.get_performance_level = pp_dpm_get_performance_level,
	.get_current_power_state = pp_dpm_get_current_power_state,
	.get_sclk = pp_dpm_get_sclk,
	.get_mclk = pp_dpm_get_mclk,
	.powergate_vce = pp_dpm_powergate_vce,
	.powergate_uvd = pp_dpm_powergate_uvd,
	.dispatch_tasks = pp_dpm_dispatch_tasks,
	.set_fan_control_mode = pp_dpm_set_fan_control_mode,
	.get_fan_control_mode = pp_dpm_get_fan_control_mode,
	.set_fan_speed_percent = pp_dpm_set_fan_speed_percent,
	.get_fan_speed_percent = pp_dpm_get_fan_speed_percent,
	.get_pp_num_states = pp_dpm_get_pp_num_states,
	.get_pp_table = pp_dpm_get_pp_table,
	.set_pp_table = pp_dpm_set_pp_table,
	.force_clock_level = pp_dpm_force_clock_level,
	.print_clock_levels = pp_dpm_print_clock_levels,
	.get_sclk_od = pp_dpm_get_sclk_od,
	.set_sclk_od = pp_dpm_set_sclk_od,
	.get_mclk_od = pp_dpm_get_mclk_od,
	.set_mclk_od = pp_dpm_set_mclk_od,
	.read_sensor = pp_dpm_read_sensor,
};

static int amd_pp_instance_init(struct amd_pp_init *pp_init,
				struct amd_powerplay *amd_pp)
{
	int ret;
	struct pp_instance *handle;

	handle = kzalloc(sizeof(struct pp_instance), GFP_KERNEL);
	if (handle == NULL)
		return -ENOMEM;

	handle->pp_valid = PP_VALID;

	ret = smum_init(pp_init, handle);
	if (ret)
		goto fail_smum;

	ret = hwmgr_init(pp_init, handle);
	if (ret)
		goto fail_hwmgr;

	ret = eventmgr_init(handle);
	if (ret)
		goto fail_eventmgr;

	amd_pp->pp_handle = handle;
	return 0;

fail_eventmgr:
	hwmgr_fini(handle->hwmgr);
fail_hwmgr:
	smum_fini(handle->smu_mgr);
fail_smum:
	kfree(handle);
	return ret;
}

static int amd_pp_instance_fini(void *handle)
{
	struct pp_instance *instance = (struct pp_instance *)handle;

	if (instance == NULL)
		return -EINVAL;

	eventmgr_fini(instance->eventmgr);

	hwmgr_fini(instance->hwmgr);

	smum_fini(instance->smu_mgr);

	kfree(handle);
	return 0;
}

int amd_powerplay_init(struct amd_pp_init *pp_init,
		       struct amd_powerplay *amd_pp)
{
	int ret;

	if (pp_init == NULL || amd_pp == NULL)
		return -EINVAL;

	ret = amd_pp_instance_init(pp_init, amd_pp);

	if (ret)
		return ret;

	amd_pp->ip_funcs = &pp_ip_funcs;
	amd_pp->pp_funcs = &pp_dpm_funcs;

	return 0;
}

int amd_powerplay_fini(void *handle)
{
	amd_pp_instance_fini(handle);

	return 0;
}

int amd_powerplay_reset(void *handle)
{
	struct pp_instance *instance = (struct pp_instance *)handle;
	struct pp_eventmgr *eventmgr;
	struct pem_event_data event_data = { {0} };
	int ret;

	if (instance == NULL)
		return -EINVAL;

	eventmgr = instance->eventmgr;
	if (!eventmgr || !eventmgr->pp_eventmgr_fini)
		return -EINVAL;

	eventmgr->pp_eventmgr_fini(eventmgr);

	ret = pp_sw_fini(handle);
	if (ret)
		return ret;

	kfree(instance->hwmgr->ps);

	ret = pp_sw_init(handle);
	if (ret)
		return ret;

	hw_init_power_state_table(instance->hwmgr);

	if (eventmgr == NULL || eventmgr->pp_eventmgr_init == NULL)
		return -EINVAL;

	ret = eventmgr->pp_eventmgr_init(eventmgr);
	if (ret)
		return ret;

	return pem_handle_event(eventmgr, AMD_PP_EVENT_COMPLETE_INIT, &event_data);
}

/* export this function to DAL */

int amd_powerplay_display_configuration_change(void *handle,
	const struct amd_pp_display_configuration *display_config)
{
	struct pp_hwmgr  *hwmgr;

	PP_CHECK((struct pp_instance *)handle);

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	phm_store_dal_configuration_data(hwmgr, display_config);

	return 0;
}

int amd_powerplay_get_display_power_level(void *handle,
		struct amd_pp_simple_clock_info *output)
{
	struct pp_hwmgr  *hwmgr;

	PP_CHECK((struct pp_instance *)handle);

	if (output == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	return phm_get_dal_power_level(hwmgr, output);
}

int amd_powerplay_get_current_clocks(void *handle,
		struct amd_pp_clock_info *clocks)
{
	struct pp_hwmgr  *hwmgr;
	struct amd_pp_simple_clock_info simple_clocks;
	struct pp_clock_info hw_clocks;

	PP_CHECK((struct pp_instance *)handle);

	if (clocks == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	phm_get_dal_power_level(hwmgr, &simple_clocks);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_PowerContainment)) {
		if (0 != phm_get_clock_info(hwmgr, &hwmgr->current_ps->hardware, &hw_clocks, PHM_PerformanceLevelDesignation_PowerContainment))
			PP_ASSERT_WITH_CODE(0, "Error in PHM_GetPowerContainmentClockInfo", return -1);
	} else {
		if (0 != phm_get_clock_info(hwmgr, &hwmgr->current_ps->hardware, &hw_clocks, PHM_PerformanceLevelDesignation_Activity))
			PP_ASSERT_WITH_CODE(0, "Error in PHM_GetClockInfo", return -1);
	}

	clocks->min_engine_clock = hw_clocks.min_eng_clk;
	clocks->max_engine_clock = hw_clocks.max_eng_clk;
	clocks->min_memory_clock = hw_clocks.min_mem_clk;
	clocks->max_memory_clock = hw_clocks.max_mem_clk;
	clocks->min_bus_bandwidth = hw_clocks.min_bus_bandwidth;
	clocks->max_bus_bandwidth = hw_clocks.max_bus_bandwidth;

	clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
	clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;

	clocks->max_clocks_state = simple_clocks.level;

	if (0 == phm_get_current_shallow_sleep_clocks(hwmgr, &hwmgr->current_ps->hardware, &hw_clocks)) {
		clocks->max_engine_clock_in_sr = hw_clocks.max_eng_clk;
		clocks->min_engine_clock_in_sr = hw_clocks.min_eng_clk;
	}

	return 0;

}

int amd_powerplay_get_clock_by_type(void *handle, enum amd_pp_clock_type type, struct amd_pp_clocks *clocks)
{
	int result = -1;

	struct pp_hwmgr *hwmgr;

	PP_CHECK((struct pp_instance *)handle);

	if (clocks == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	result = phm_get_clock_by_type(hwmgr, type, clocks);

	return result;
}

int amd_powerplay_get_display_mode_validation_clocks(void *handle,
		struct amd_pp_simple_clock_info *clocks)
{
	int result = -1;
	struct pp_hwmgr  *hwmgr;

	PP_CHECK((struct pp_instance *)handle);

	if (clocks == NULL)
		return -EINVAL;

	hwmgr = ((struct pp_instance *)handle)->hwmgr;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_DynamicPatchPowerState))
		result = phm_get_max_high_clocks(hwmgr, clocks);

	return result;
}

