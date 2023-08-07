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

static int vega20_disable_fan_control_feature(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = hwmgr->backend;
	int ret = 0;

	if (data->smu_features[GNLD_FAN_CONTROL].supported) {
		ret = vega20_enable_smc_features(
				hwmgr, false,
				data->smu_features[GNLD_FAN_CONTROL].
				smu_feature_bitmap);
		PP_ASSERT_WITH_CODE(!ret,
				"Disable FAN CONTROL feature Failed!",
				return ret);
		data->smu_features[GNLD_FAN_CONTROL].enabled = false;
	}

	return ret;
}

int vega20_fan_ctrl_stop_smc_fan_control(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_FAN_CONTROL].supported)
		return vega20_disable_fan_control_feature(hwmgr);

	return 0;
}

static int vega20_enable_fan_control_feature(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = hwmgr->backend;
	int ret = 0;

	if (data->smu_features[GNLD_FAN_CONTROL].supported) {
		ret = vega20_enable_smc_features(
				hwmgr, true,
				data->smu_features[GNLD_FAN_CONTROL].
				smu_feature_bitmap);
		PP_ASSERT_WITH_CODE(!ret,
				"Enable FAN CONTROL feature Failed!",
				return ret);
		data->smu_features[GNLD_FAN_CONTROL].enabled = true;
	}

	return ret;
}

int vega20_fan_ctrl_start_smc_fan_control(struct pp_hwmgr *hwmgr)
{
	struct vega20_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_FAN_CONTROL].supported)
		return vega20_enable_fan_control_feature(hwmgr);

	return 0;
}

static int vega20_fan_ctrl_set_static_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	struct amdgpu_device *adev = hwmgr->adev;

	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
			REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				CG_FDO_CTRL2, TMIN, 0));
	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
			REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				CG_FDO_CTRL2, FDO_PWM_MODE, mode));

	return 0;
}

static int vega20_get_current_rpm(struct pp_hwmgr *hwmgr, uint32_t *current_rpm)
{
	int ret = 0;

	PP_ASSERT_WITH_CODE((ret = smum_send_msg_to_smc(hwmgr,
				PPSMC_MSG_GetCurrentRpm,
				current_rpm)) == 0,
			"Attempt to get current RPM from SMC Failed!",
			return ret);

	return 0;
}

int vega20_fan_ctrl_get_fan_speed_pwm(struct pp_hwmgr *hwmgr,
		uint32_t *speed)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t duty100, duty;
	uint64_t tmp64;

	duty100 = REG_GET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL1),
				CG_FDO_CTRL1, FMAX_DUTY100);
	duty = REG_GET_FIELD(RREG32_SOC15(THM, 0, mmCG_THERMAL_STATUS),
				CG_THERMAL_STATUS, FDO_PWM_DUTY);

	if (!duty100)
		return -EINVAL;

	tmp64 = (uint64_t)duty * 255;
	do_div(tmp64, duty100);
	*speed = MIN((uint32_t)tmp64, 255);

	return 0;
}

int vega20_fan_ctrl_set_fan_speed_pwm(struct pp_hwmgr *hwmgr,
		uint32_t speed)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t duty100;
	uint32_t duty;
	uint64_t tmp64;

	speed = MIN(speed, 255);

	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
		vega20_fan_ctrl_stop_smc_fan_control(hwmgr);

	duty100 = REG_GET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL1),
				    CG_FDO_CTRL1, FMAX_DUTY100);

	if (duty100 == 0)
		return -EINVAL;

	tmp64 = (uint64_t)speed * duty100;
	do_div(tmp64, 255);
	duty = (uint32_t)tmp64;

	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL0,
		REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL0),
			CG_FDO_CTRL0, FDO_STATIC_DUTY, duty));

	return vega20_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
}

int vega20_fan_ctrl_get_fan_speed_info(struct pp_hwmgr *hwmgr,
		struct phm_fan_speed_info *fan_speed_info)
{
	memset(fan_speed_info, 0, sizeof(*fan_speed_info));
	fan_speed_info->supports_percent_read = true;
	fan_speed_info->supports_percent_write = true;
	fan_speed_info->supports_rpm_read = true;
	fan_speed_info->supports_rpm_write = true;

	return 0;
}

int vega20_fan_ctrl_get_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t *speed)
{
	*speed = 0;

	return vega20_get_current_rpm(hwmgr, speed);
}

int vega20_fan_ctrl_set_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t speed)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t tach_period, crystal_clock_freq;
	int result = 0;

	if (!speed)
		return -EINVAL;

	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl)) {
		result = vega20_fan_ctrl_stop_smc_fan_control(hwmgr);
		if (result)
			return result;
	}

	crystal_clock_freq = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);
	tach_period = 60 * crystal_clock_freq * 10000 / (8 * speed);
	WREG32_SOC15(THM, 0, mmCG_TACH_CTRL,
			REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_TACH_CTRL),
				CG_TACH_CTRL, TARGET_PERIOD,
				tach_period));

	return vega20_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC_RPM);
}

/**
 * vega20_thermal_get_temperature - Reads the remote temperature from the SIslands thermal controller.
 *
 * @hwmgr: The address of the hardware manager.
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
 * vega20_thermal_set_temperature_range - Set the requested temperature range for high and low alert signals
 *
 * @hwmgr: The address of the hardware manager.
 * @range: Temperature range to be programmed for high and low alert signals
 * Exception: PP_Result_BadInput if the input data is not valid.
 */
static int vega20_thermal_set_temperature_range(struct pp_hwmgr *hwmgr,
		struct PP_TemperatureRange *range)
{
	struct phm_ppt_v3_information *pptable_information =
		(struct phm_ppt_v3_information *)hwmgr->pptable;
	struct amdgpu_device *adev = hwmgr->adev;
	int low = VEGA20_THERMAL_MINIMUM_ALERT_TEMP;
	int high = VEGA20_THERMAL_MAXIMUM_ALERT_TEMP;
	uint32_t val;

	/* compare them in unit celsius degree */
	if (low < range->min / PP_TEMPERATURE_UNITS_PER_CENTIGRADES)
		low = range->min / PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	if (high > pptable_information->us_software_shutdown_temp)
		high = pptable_information->us_software_shutdown_temp;

	if (low > high)
		return -EINVAL;

	val = RREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL);

	val = CGS_REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, MAX_IH_CREDIT, 5);
	val = CGS_REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_IH_HW_ENA, 1);
	val = CGS_REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTH, high);
	val = CGS_REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTL, low);
	val &= ~THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK;
	val &= ~THM_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK;
	val &= ~THM_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK;

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL, val);

	return 0;
}

/**
 * vega20_thermal_enable_alert - Enable thermal alerts on the RV770 thermal controller.
 *
 * @hwmgr: The address of the hardware manager.
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
 * vega20_thermal_disable_alert - Disable thermal alerts on the RV770 thermal controller.
 * @hwmgr: The address of the hardware manager.
 */
int vega20_thermal_disable_alert(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, 0);

	return 0;
}

/**
 * vega20_thermal_stop_thermal_controller - Uninitialize the thermal controller.
 * Currently just disables alerts.
 * @hwmgr: The address of the hardware manager.
 */
int vega20_thermal_stop_thermal_controller(struct pp_hwmgr *hwmgr)
{
	int result = vega20_thermal_disable_alert(hwmgr);

	return result;
}

/**
 * vega20_thermal_setup_fan_table - Set up the fan table to control the fan using the SMC.
 * @hwmgr:  the address of the powerplay hardware manager.
 */
static int vega20_thermal_setup_fan_table(struct pp_hwmgr *hwmgr)
{
	int ret;
	struct vega20_hwmgr *data = (struct vega20_hwmgr *)(hwmgr->backend);
	PPTable_t *table = &(data->smc_state_table.pp_table);

	ret = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetFanTemperatureTarget,
				(uint32_t)table->FanTargetTemperature,
				NULL);

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
