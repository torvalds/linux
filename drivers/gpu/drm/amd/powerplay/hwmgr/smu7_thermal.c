/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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

#include <asm/div64.h>
#include "smu7_thermal.h"
#include "smu7_hwmgr.h"
#include "smu7_common.h"

int smu7_fan_ctrl_get_fan_speed_info(struct pp_hwmgr *hwmgr,
		struct phm_fan_speed_info *fan_speed_info)
{
	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return -ENODEV;

	fan_speed_info->supports_percent_read = true;
	fan_speed_info->supports_percent_write = true;
	fan_speed_info->min_percent = 0;
	fan_speed_info->max_percent = 100;

	if (PP_CAP(PHM_PlatformCaps_FanSpeedInTableIsRPM) &&
	    hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution) {
		fan_speed_info->supports_rpm_read = true;
		fan_speed_info->supports_rpm_write = true;
		fan_speed_info->min_rpm = hwmgr->thermal_controller.fanInfo.ulMinRPM;
		fan_speed_info->max_rpm = hwmgr->thermal_controller.fanInfo.ulMaxRPM;
	} else {
		fan_speed_info->min_rpm = 0;
		fan_speed_info->max_rpm = 0;
	}

	return 0;
}

int smu7_fan_ctrl_get_fan_speed_percent(struct pp_hwmgr *hwmgr,
		uint32_t *speed)
{
	uint32_t duty100;
	uint32_t duty;
	uint64_t tmp64;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return -ENODEV;

	duty100 = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL1, FMAX_DUTY100);
	duty = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_STATUS, FDO_PWM_DUTY);

	if (duty100 == 0)
		return -EINVAL;


	tmp64 = (uint64_t)duty * 100;
	do_div(tmp64, duty100);
	*speed = (uint32_t)tmp64;

	if (*speed > 100)
		*speed = 100;

	return 0;
}

int smu7_fan_ctrl_get_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t *speed)
{
	uint32_t tach_period;
	uint32_t crystal_clock_freq;

	if (hwmgr->thermal_controller.fanInfo.bNoFan ||
	    !hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution)
		return -ENODEV;

	tach_period = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_TACH_STATUS, TACH_PERIOD);

	if (tach_period == 0)
		return -EINVAL;

	crystal_clock_freq = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);

	*speed = 60 * crystal_clock_freq * 10000 / tach_period;

	return 0;
}

/**
* Set Fan Speed Control to static mode, so that the user can decide what speed to use.
* @param    hwmgr  the address of the powerplay hardware manager.
*           mode    the fan control mode, 0 default, 1 by percent, 5, by RPM
* @exception Should always succeed.
*/
int smu7_fan_ctrl_set_static_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	if (hwmgr->fan_ctrl_is_in_default_mode) {
		hwmgr->fan_ctrl_default_mode =
				PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
						CG_FDO_CTRL2, FDO_PWM_MODE);
		hwmgr->tmin =
				PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
						CG_FDO_CTRL2, TMIN);
		hwmgr->fan_ctrl_is_in_default_mode = false;
	}

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL2, TMIN, 0);
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL2, FDO_PWM_MODE, mode);

	return 0;
}

/**
* Reset Fan Speed Control to default mode.
* @param    hwmgr  the address of the powerplay hardware manager.
* @exception Should always succeed.
*/
int smu7_fan_ctrl_set_default_mode(struct pp_hwmgr *hwmgr)
{
	if (!hwmgr->fan_ctrl_is_in_default_mode) {
		PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_FDO_CTRL2, FDO_PWM_MODE, hwmgr->fan_ctrl_default_mode);
		PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_FDO_CTRL2, TMIN, hwmgr->tmin);
		hwmgr->fan_ctrl_is_in_default_mode = true;
	}

	return 0;
}

int smu7_fan_ctrl_start_smc_fan_control(struct pp_hwmgr *hwmgr)
{
	int result;

	if (PP_CAP(PHM_PlatformCaps_ODFuzzyFanControlSupport)) {
		result = smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_StartFanControl,
					FAN_CONTROL_FUZZY, NULL);

		if (PP_CAP(PHM_PlatformCaps_FanSpeedInTableIsRPM))
			hwmgr->hwmgr_func->set_max_fan_rpm_output(hwmgr,
					hwmgr->thermal_controller.
					advanceFanControlParameters.usMaxFanRPM);
		else
			hwmgr->hwmgr_func->set_max_fan_pwm_output(hwmgr,
					hwmgr->thermal_controller.
					advanceFanControlParameters.usMaxFanPWM);

	} else {
		result = smum_send_msg_to_smc_with_parameter(hwmgr, PPSMC_StartFanControl,
					FAN_CONTROL_TABLE, NULL);
	}

	if (!result && hwmgr->thermal_controller.
			advanceFanControlParameters.ucTargetTemperature)
		result = smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetFanTemperatureTarget,
				hwmgr->thermal_controller.
				advanceFanControlParameters.ucTargetTemperature,
				NULL);
	hwmgr->fan_ctrl_enabled = true;

	return result;
}


int smu7_fan_ctrl_stop_smc_fan_control(struct pp_hwmgr *hwmgr)
{
	hwmgr->fan_ctrl_enabled = false;
	return smum_send_msg_to_smc(hwmgr, PPSMC_StopFanControl, NULL);
}

/**
* Set Fan Speed in percent.
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    speed is the percentage value (0% - 100%) to be set.
* @exception Fails is the 100% setting appears to be 0.
*/
int smu7_fan_ctrl_set_fan_speed_percent(struct pp_hwmgr *hwmgr,
		uint32_t speed)
{
	uint32_t duty100;
	uint32_t duty;
	uint64_t tmp64;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	if (speed > 100)
		speed = 100;

	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
		smu7_fan_ctrl_stop_smc_fan_control(hwmgr);

	duty100 = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL1, FMAX_DUTY100);

	if (duty100 == 0)
		return -EINVAL;

	tmp64 = (uint64_t)speed * duty100;
	do_div(tmp64, 100);
	duty = (uint32_t)tmp64;

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL0, FDO_STATIC_DUTY, duty);

	return smu7_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
}

/**
* Reset Fan Speed to default.
* @param    hwmgr  the address of the powerplay hardware manager.
* @exception Always succeeds.
*/
int smu7_fan_ctrl_reset_fan_speed_to_default(struct pp_hwmgr *hwmgr)
{
	int result;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl)) {
		result = smu7_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
		if (!result)
			result = smu7_fan_ctrl_start_smc_fan_control(hwmgr);
	} else
		result = smu7_fan_ctrl_set_default_mode(hwmgr);

	return result;
}

/**
* Set Fan Speed in RPM.
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    speed is the percentage value (min - max) to be set.
* @exception Fails is the speed not lie between min and max.
*/
int smu7_fan_ctrl_set_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t speed)
{
	uint32_t tach_period;
	uint32_t crystal_clock_freq;

	if (hwmgr->thermal_controller.fanInfo.bNoFan ||
			(hwmgr->thermal_controller.fanInfo.
			ucTachometerPulsesPerRevolution == 0) ||
			speed == 0 ||
			(speed < hwmgr->thermal_controller.fanInfo.ulMinRPM) ||
			(speed > hwmgr->thermal_controller.fanInfo.ulMaxRPM))
		return 0;

	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
		smu7_fan_ctrl_stop_smc_fan_control(hwmgr);

	crystal_clock_freq = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);

	tach_period = 60 * crystal_clock_freq * 10000 / (8 * speed);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_TACH_CTRL, TARGET_PERIOD, tach_period);

	return smu7_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC_RPM);
}

/**
* Reads the remote temperature from the SIslands thermal controller.
*
* @param    hwmgr The address of the hardware manager.
*/
int smu7_thermal_get_temperature(struct pp_hwmgr *hwmgr)
{
	int temp;

	temp = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_MULT_THERMAL_STATUS, CTF_TEMP);

	/* Bit 9 means the reading is lower than the lowest usable value. */
	if (temp & 0x200)
		temp = SMU7_THERMAL_MAXIMUM_TEMP_READING;
	else
		temp = temp & 0x1ff;

	temp *= PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return temp;
}

/**
* Set the requested temperature range for high and low alert signals
*
* @param    hwmgr The address of the hardware manager.
* @param    range Temperature range to be programmed for high and low alert signals
* @exception PP_Result_BadInput if the input data is not valid.
*/
static int smu7_thermal_set_temperature_range(struct pp_hwmgr *hwmgr,
		int low_temp, int high_temp)
{
	int low = SMU7_THERMAL_MINIMUM_ALERT_TEMP *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	int high = SMU7_THERMAL_MAXIMUM_ALERT_TEMP *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	if (low < low_temp)
		low = low_temp;
	if (high > high_temp)
		high = high_temp;

	if (low > high)
		return -EINVAL;

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, DIG_THERM_INTH,
			(high / PP_TEMPERATURE_UNITS_PER_CENTIGRADES));
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, DIG_THERM_INTL,
			(low / PP_TEMPERATURE_UNITS_PER_CENTIGRADES));
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_CTRL, DIG_THERM_DPM,
			(high / PP_TEMPERATURE_UNITS_PER_CENTIGRADES));

	return 0;
}

/**
* Programs thermal controller one-time setting registers
*
* @param    hwmgr The address of the hardware manager.
*/
static int smu7_thermal_initialize(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution)
		PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_TACH_CTRL, EDGE_PER_REV,
				hwmgr->thermal_controller.fanInfo.
				ucTachometerPulsesPerRevolution - 1);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL2, TACH_PWM_RESP_RATE, 0x28);

	return 0;
}

/**
* Enable thermal alerts on the RV770 thermal controller.
*
* @param    hwmgr The address of the hardware manager.
*/
static void smu7_thermal_enable_alert(struct pp_hwmgr *hwmgr)
{
	uint32_t alert;

	alert = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, THERM_INT_MASK);
	alert &= ~(SMU7_THERMAL_HIGH_ALERT_MASK | SMU7_THERMAL_LOW_ALERT_MASK);
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, THERM_INT_MASK, alert);

	/* send message to SMU to enable internal thermal interrupts */
	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_Thermal_Cntl_Enable, NULL);
}

/**
* Disable thermal alerts on the RV770 thermal controller.
* @param    hwmgr The address of the hardware manager.
*/
int smu7_thermal_disable_alert(struct pp_hwmgr *hwmgr)
{
	uint32_t alert;

	alert = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, THERM_INT_MASK);
	alert |= (SMU7_THERMAL_HIGH_ALERT_MASK | SMU7_THERMAL_LOW_ALERT_MASK);
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, THERM_INT_MASK, alert);

	/* send message to SMU to disable internal thermal interrupts */
	return smum_send_msg_to_smc(hwmgr, PPSMC_MSG_Thermal_Cntl_Disable, NULL);
}

/**
* Uninitialize the thermal controller.
* Currently just disables alerts.
* @param    hwmgr The address of the hardware manager.
*/
int smu7_thermal_stop_thermal_controller(struct pp_hwmgr *hwmgr)
{
	int result = smu7_thermal_disable_alert(hwmgr);

	if (!hwmgr->thermal_controller.fanInfo.bNoFan)
		smu7_fan_ctrl_set_default_mode(hwmgr);

	return result;
}

/**
* Start the fan control on the SMC.
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput the pointer to input data
* @param    pOutput the pointer to output data
* @param    pStorage the pointer to temporary storage
* @param    Result the last failure code
* @return   result from set temperature range routine
*/
static int smu7_thermal_start_smc_fan_control(struct pp_hwmgr *hwmgr)
{
/* If the fantable setup has failed we could have disabled
 * PHM_PlatformCaps_MicrocodeFanControl even after
 * this function was included in the table.
 * Make sure that we still think controlling the fan is OK.
*/
	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl)) {
		smu7_fan_ctrl_start_smc_fan_control(hwmgr);
		smu7_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
	}

	return 0;
}

int smu7_start_thermal_controller(struct pp_hwmgr *hwmgr,
				struct PP_TemperatureRange *range)
{
	int ret = 0;

	if (range == NULL)
		return -EINVAL;

	smu7_thermal_initialize(hwmgr);
	ret = smu7_thermal_set_temperature_range(hwmgr, range->min, range->max);
	if (ret)
		return -EINVAL;
	smu7_thermal_enable_alert(hwmgr);
	ret = smum_thermal_avfs_enable(hwmgr);
	if (ret)
		return -EINVAL;

/* We should restrict performance levels to low before we halt the SMC.
 * On the other hand we are still in boot state when we do this
 * so it would be pointless.
 * If this assumption changes we have to revisit this table.
 */
	smum_thermal_setup_fan_table(hwmgr);
	smu7_thermal_start_smc_fan_control(hwmgr);
	return 0;
}



int smu7_thermal_ctrl_uninitialize_thermal_controller(struct pp_hwmgr *hwmgr)
{
	if (!hwmgr->thermal_controller.fanInfo.bNoFan)
		smu7_fan_ctrl_set_default_mode(hwmgr);
	return 0;
}

