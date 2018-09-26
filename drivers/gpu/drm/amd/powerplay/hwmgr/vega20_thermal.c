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
 */

#include "vega20_thermal.h"
#include "vega20_hwmgr.h"
#include "vega20_smumgr.h"
#include "vega20_ppsmc.h"
#include "vega20_inc.h"
#include "soc15_common.h"
#include "pp_debug.h"

static int vega20_get_current_rpm(struct pp_hwmgr *hwmgr, uint32_t *current_rpm)
{
	int ret = 0;

	PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc(hwmgr,
				PPSMC_MSG_GetCurrentRpm)) == 0,
			"Attempt to get current RPM from SMC Failed!",
			return ret);
	PP_ASSERT_WITH_CODE((ret = vega20_read_arg_from_smc(hwmgr,
			current_rpm)) == 0,
			"Attempt to read current RPM from SMC Failed!",
			return ret);

	return 0;
}

int vega20_fan_ctrl_get_fan_speed_info(struct pp_hwmgr *hwmgr,
		struct phm_fan_speed_info *fan_speed_info)
{
	memset(fan_speed_info, 0, sizeof(*fan_speed_info));
	fan_speed_info->supports_percent_read = false;
	fan_speed_info->supports_percent_write = false;
	fan_speed_info->supports_rpm_read = true;
	fan_speed_info->supports_rpm_write = true;

	return 0;
}

int vega20_fan_ctrl_get_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t *speed)
{
	*speed = 0;

	return vega20_get_current_rpm(hwmgr, speed);
}

/**
* Reads the remote temperature from the SIslands thermal controller.
*
* @param    hwmgr The address of the hardware manager.
*/
int vega20_thermal_get_temperature(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int temp = 0;

	temp = RREG32_SOC15(THM, 0, mmCG_MULT_THERMAL_STATUS);

	temp = (temp & CG_MULT_THERMAL_STATUS__CTF_TEMP_MASK) >>
			CG_MULT_THERMAL_STATUS__CTF_TEMP__SHIFT;

	temp = temp & 0x1ff;

	temp *= PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	return temp;
}

/**
* Set the requested temperature range for high and low alert signals
*
* @param    hwmgr The address of the hardware manager.
* @param    range Temperature range to be programmed for
*           high and low alert signals
* @exception PP_Result_BadInput if the input data is not valid.
*/
static int vega20_thermal_set_temperature_range(struct pp_hwmgr *hwmgr,
		struct PP_TemperatureRange *range)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int low = VEGA20_THERMAL_MINIMUM_ALERT_TEMP *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	int high = VEGA20_THERMAL_MAXIMUM_ALERT_TEMP *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	uint32_t val;

	if (low < range->min)
		low = range->min;
	if (high > range->max)
		high = range->max;

	if (low > high)
		return -EINVAL;

	val = RREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL);

	val = CGS_REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, MAX_IH_CREDIT, 5);
	val = CGS_REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_IH_HW_ENA, 1);
	val = CGS_REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTH, (high / PP_TEMPERATURE_UNITS_PER_CENTIGRADES));
	val = CGS_REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTL, (low / PP_TEMPERATURE_UNITS_PER_CENTIGRADES));
	val = val & (~THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK);

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL, val);

	return 0;
}

/**
* Enable thermal alerts on the RV770 thermal controller.
*
* @param    hwmgr The address of the hardware manager.
*/
static int vega20_thermal_enable_alert(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t val = 0;

	val |= (1 << THM_THERMAL_INT_ENA__THERM_INTH_CLR__SHIFT);
	val |= (1 << THM_THERMAL_INT_ENA__THERM_INTL_CLR__SHIFT);
	val |= (1 << THM_THERMAL_INT_ENA__THERM_TRIGGER_CLR__SHIFT);

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, val);

	return 0;
}

/**
* Disable thermal alerts on the RV770 thermal controller.
* @param    hwmgr The address of the hardware manager.
*/
int vega20_thermal_disable_alert(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, 0);

	return 0;
}

/**
* Uninitialize the thermal controller.
* Currently just disables alerts.
* @param    hwmgr The address of the hardware manager.
*/
int vega20_thermal_stop_thermal_controller(struct pp_hwmgr *hwmgr)
{
	int result = vega20_thermal_disable_alert(hwmgr);

	return result;
}

/**
* Set up the fan table to control the fan using the SMC.
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput the pointer to input data
* @param    pOutput the pointer to output data
* @param    pStorage the pointer to temporary storage
* @param    Result the last failure code
* @return   result from set temperature range routine
*/
static int vega20_thermal_setup_fan_table(struct pp_hwmgr *hwmgr)
{
	int ret;
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	PPTable_t *table = &(data->smc_state_table.pp_table);

	ret = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetFanTemperatureTarget,
				(uint32_t)table->FanTargetTemperature);

	return ret;
}

int vega20_start_thermal_controller(struct pp_hwmgr *hwmgr,
				struct PP_TemperatureRange *range)
{
	int ret = 0;

	if (range == NULL)
		return -EINVAL;

	ret = vega20_thermal_set_temperature_range(hwmgr, range);
	if (ret)
		return ret;

	ret = vega20_thermal_enable_alert(hwmgr);
	if (ret)
		return ret;

	ret = vega20_thermal_setup_fan_table(hwmgr);

	return ret;
};
