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
#include "polaris10_thermal.h"
#include "polaris10_hwmgr.h"
#include "polaris10_smumgr.h"
#include "polaris10_ppsmc.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"

int polaris10_fan_ctrl_get_fan_speed_info(struct pp_hwmgr *hwmgr,
		struct phm_fan_speed_info *fan_speed_info)
{
	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	fan_speed_info->supports_percent_read = true;
	fan_speed_info->supports_percent_write = true;
	fan_speed_info->min_percent = 0;
	fan_speed_info->max_percent = 100;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_FanSpeedInTableIsRPM) &&
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

int polaris10_fan_ctrl_get_fan_speed_percent(struct pp_hwmgr *hwmgr,
		uint32_t *speed)
{
	uint32_t duty100;
	uint32_t duty;
	uint64_t tmp64;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

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

int polaris10_fan_ctrl_get_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t *speed)
{
	uint32_t tach_period;
	uint32_t crystal_clock_freq;

	if (hwmgr->thermal_controller.fanInfo.bNoFan ||
			(hwmgr->thermal_controller.fanInfo.
				ucTachometerPulsesPerRevolution == 0))
		return 0;

	tach_period = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_TACH_STATUS, TACH_PERIOD);

	if (tach_period == 0)
		return -EINVAL;

	crystal_clock_freq = tonga_get_xclk(hwmgr);

	*speed = 60 * crystal_clock_freq * 10000 / tach_period;

	return 0;
}

/**
* Set Fan Speed Control to static mode, so that the user can decide what speed to use.
* @param    hwmgr  the address of the powerplay hardware manager.
*           mode    the fan control mode, 0 default, 1 by percent, 5, by RPM
* @exception Should always succeed.
*/
int polaris10_fan_ctrl_set_static_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{

	if (hwmgr->fan_ctrl_is_in_default_mode) {
		hwmgr->fan_ctrl_default_mode =
				PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device,	CGS_IND_REG__SMC,
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
int polaris10_fan_ctrl_set_default_mode(struct pp_hwmgr *hwmgr)
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

int polaris10_fan_ctrl_start_smc_fan_control(struct pp_hwmgr *hwmgr)
{
	int result;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_ODFuzzyFanControlSupport)) {
		cgs_write_register(hwmgr->device, mmSMC_MSG_ARG_0, FAN_CONTROL_FUZZY);
		result = smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_StartFanControl);

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_FanSpeedInTableIsRPM))
			hwmgr->hwmgr_func->set_max_fan_rpm_output(hwmgr,
					hwmgr->thermal_controller.
					advanceFanControlParameters.usMaxFanRPM);
		else
			hwmgr->hwmgr_func->set_max_fan_pwm_output(hwmgr,
					hwmgr->thermal_controller.
					advanceFanControlParameters.usMaxFanPWM);

	} else {
		cgs_write_register(hwmgr->device, mmSMC_MSG_ARG_0, FAN_CONTROL_TABLE);
		result = smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_StartFanControl);
	}

	if (!result && hwmgr->thermal_controller.
			advanceFanControlParameters.ucTargetTemperature)
		result = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetFanTemperatureTarget,
				hwmgr->thermal_controller.
				advanceFanControlParameters.ucTargetTemperature);

	return result;
}


int polaris10_fan_ctrl_stop_smc_fan_control(struct pp_hwmgr *hwmgr)
{
	return smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_StopFanControl);
}

/**
* Set Fan Speed in percent.
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    speed is the percentage value (0% - 100%) to be set.
* @exception Fails is the 100% setting appears to be 0.
*/
int polaris10_fan_ctrl_set_fan_speed_percent(struct pp_hwmgr *hwmgr,
		uint32_t speed)
{
	uint32_t duty100;
	uint32_t duty;
	uint64_t tmp64;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	if (speed > 100)
		speed = 100;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MicrocodeFanControl))
		polaris10_fan_ctrl_stop_smc_fan_control(hwmgr);

	duty100 = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL1, FMAX_DUTY100);

	if (duty100 == 0)
		return -EINVAL;

	tmp64 = (uint64_t)speed * duty100;
	do_div(tmp64, 100);
	duty = (uint32_t)tmp64;

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL0, FDO_STATIC_DUTY, duty);

	return polaris10_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
}

/**
* Reset Fan Speed to default.
* @param    hwmgr  the address of the powerplay hardware manager.
* @exception Always succeeds.
*/
int polaris10_fan_ctrl_reset_fan_speed_to_default(struct pp_hwmgr *hwmgr)
{
	int result;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MicrocodeFanControl)) {
		result = polaris10_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
		if (!result)
			result = polaris10_fan_ctrl_start_smc_fan_control(hwmgr);
	} else
		result = polaris10_fan_ctrl_set_default_mode(hwmgr);

	return result;
}

/**
* Set Fan Speed in RPM.
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    speed is the percentage value (min - max) to be set.
* @exception Fails is the speed not lie between min and max.
*/
int polaris10_fan_ctrl_set_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t speed)
{
	uint32_t tach_period;
	uint32_t crystal_clock_freq;

	if (hwmgr->thermal_controller.fanInfo.bNoFan ||
			(hwmgr->thermal_controller.fanInfo.
			ucTachometerPulsesPerRevolution == 0) ||
			(speed < hwmgr->thermal_controller.fanInfo.ulMinRPM) ||
			(speed > hwmgr->thermal_controller.fanInfo.ulMaxRPM))
		return 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MicrocodeFanControl))
		polaris10_fan_ctrl_stop_smc_fan_control(hwmgr);

	crystal_clock_freq = tonga_get_xclk(hwmgr);

	tach_period = 60 * crystal_clock_freq * 10000 / (8 * speed);

	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				CG_TACH_STATUS, TACH_PERIOD, tach_period);

	return polaris10_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
}

/**
* Reads the remote temperature from the SIslands thermal controller.
*
* @param    hwmgr The address of the hardware manager.
*/
int polaris10_thermal_get_temperature(struct pp_hwmgr *hwmgr)
{
	int temp;

	temp = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_MULT_THERMAL_STATUS, CTF_TEMP);

	/* Bit 9 means the reading is lower than the lowest usable value. */
	if (temp & 0x200)
		temp = POLARIS10_THERMAL_MAXIMUM_TEMP_READING;
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
static int polaris10_thermal_set_temperature_range(struct pp_hwmgr *hwmgr,
		uint32_t low_temp, uint32_t high_temp)
{
	uint32_t low = POLARIS10_THERMAL_MINIMUM_ALERT_TEMP *
			PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	uint32_t high = POLARIS10_THERMAL_MAXIMUM_ALERT_TEMP *
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
static int polaris10_thermal_initialize(struct pp_hwmgr *hwmgr)
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
static int polaris10_thermal_enable_alert(struct pp_hwmgr *hwmgr)
{
	uint32_t alert;

	alert = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, THERM_INT_MASK);
	alert &= ~(POLARIS10_THERMAL_HIGH_ALERT_MASK | POLARIS10_THERMAL_LOW_ALERT_MASK);
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, THERM_INT_MASK, alert);

	/* send message to SMU to enable internal thermal interrupts */
	return smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_Thermal_Cntl_Enable);
}

/**
* Disable thermal alerts on the RV770 thermal controller.
* @param    hwmgr The address of the hardware manager.
*/
static int polaris10_thermal_disable_alert(struct pp_hwmgr *hwmgr)
{
	uint32_t alert;

	alert = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, THERM_INT_MASK);
	alert |= (POLARIS10_THERMAL_HIGH_ALERT_MASK | POLARIS10_THERMAL_LOW_ALERT_MASK);
	PHM_WRITE_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_THERMAL_INT, THERM_INT_MASK, alert);

	/* send message to SMU to disable internal thermal interrupts */
	return smum_send_msg_to_smc(hwmgr->smumgr, PPSMC_MSG_Thermal_Cntl_Disable);
}

/**
* Uninitialize the thermal controller.
* Currently just disables alerts.
* @param    hwmgr The address of the hardware manager.
*/
int polaris10_thermal_stop_thermal_controller(struct pp_hwmgr *hwmgr)
{
	int result = polaris10_thermal_disable_alert(hwmgr);

	if (!hwmgr->thermal_controller.fanInfo.bNoFan)
		polaris10_fan_ctrl_set_default_mode(hwmgr);

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
int tf_polaris10_thermal_setup_fan_table(struct pp_hwmgr *hwmgr,
		void *input, void *output, void *storage, int result)
{
	struct polaris10_hwmgr *data = (struct polaris10_hwmgr *)(hwmgr->backend);
	SMU74_Discrete_FanTable fan_table = { FDO_MODE_HARDWARE };
	uint32_t duty100;
	uint32_t t_diff1, t_diff2, pwm_diff1, pwm_diff2;
	uint16_t fdo_min, slope1, slope2;
	uint32_t reference_clock;
	int res;
	uint64_t tmp64;

	if (data->fan_table_start == 0) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	duty100 = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
			CG_FDO_CTRL1, FMAX_DUTY100);

	if (duty100 == 0) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	tmp64 = hwmgr->thermal_controller.advanceFanControlParameters.
			usPWMMin * duty100;
	do_div(tmp64, 10000);
	fdo_min = (uint16_t)tmp64;

	t_diff1 = hwmgr->thermal_controller.advanceFanControlParameters.usTMed -
			hwmgr->thermal_controller.advanceFanControlParameters.usTMin;
	t_diff2 = hwmgr->thermal_controller.advanceFanControlParameters.usTHigh -
			hwmgr->thermal_controller.advanceFanControlParameters.usTMed;

	pwm_diff1 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed -
			hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin;
	pwm_diff2 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMHigh -
			hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed;

	slope1 = (uint16_t)((50 + ((16 * duty100 * pwm_diff1) / t_diff1)) / 100);
	slope2 = (uint16_t)((50 + ((16 * duty100 * pwm_diff2) / t_diff2)) / 100);

	fan_table.TempMin = cpu_to_be16((50 + hwmgr->
			thermal_controller.advanceFanControlParameters.usTMin) / 100);
	fan_table.TempMed = cpu_to_be16((50 + hwmgr->
			thermal_controller.advanceFanControlParameters.usTMed) / 100);
	fan_table.TempMax = cpu_to_be16((50 + hwmgr->
			thermal_controller.advanceFanControlParameters.usTMax) / 100);

	fan_table.Slope1 = cpu_to_be16(slope1);
	fan_table.Slope2 = cpu_to_be16(slope2);

	fan_table.FdoMin = cpu_to_be16(fdo_min);

	fan_table.HystDown = cpu_to_be16(hwmgr->
			thermal_controller.advanceFanControlParameters.ucTHyst);

	fan_table.HystUp = cpu_to_be16(1);

	fan_table.HystSlope = cpu_to_be16(1);

	fan_table.TempRespLim = cpu_to_be16(5);

	reference_clock = tonga_get_xclk(hwmgr);

	fan_table.RefreshPeriod = cpu_to_be32((hwmgr->
			thermal_controller.advanceFanControlParameters.ulCycleDelay *
			reference_clock) / 1600);

	fan_table.FdoMax = cpu_to_be16((uint16_t)duty100);

	fan_table.TempSrc = (uint8_t)PHM_READ_VFPF_INDIRECT_FIELD(
			hwmgr->device, CGS_IND_REG__SMC,
			CG_MULT_THERMAL_CTRL, TEMP_SEL);

	res = polaris10_copy_bytes_to_smc(hwmgr->smumgr, data->fan_table_start,
			(uint8_t *)&fan_table, (uint32_t)sizeof(fan_table),
			data->sram_end);

	if (!res && hwmgr->thermal_controller.
			advanceFanControlParameters.ucMinimumPWMLimit)
		res = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetFanMinPwm,
				hwmgr->thermal_controller.
				advanceFanControlParameters.ucMinimumPWMLimit);

	if (!res && hwmgr->thermal_controller.
			advanceFanControlParameters.ulMinFanSCLKAcousticLimit)
		res = smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetFanSclkTarget,
				hwmgr->thermal_controller.
				advanceFanControlParameters.ulMinFanSCLKAcousticLimit);

	if (res)
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_MicrocodeFanControl);

	return 0;
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
int tf_polaris10_thermal_start_smc_fan_control(struct pp_hwmgr *hwmgr,
		void *input, void *output, void *storage, int result)
{
/* If the fantable setup has failed we could have disabled
 * PHM_PlatformCaps_MicrocodeFanControl even after
 * this function was included in the table.
 * Make sure that we still think controlling the fan is OK.
*/
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MicrocodeFanControl)) {
		polaris10_fan_ctrl_start_smc_fan_control(hwmgr);
		polaris10_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
	}

	return 0;
}

/**
* Set temperature range for high and low alerts
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput the pointer to input data
* @param    pOutput the pointer to output data
* @param    pStorage the pointer to temporary storage
* @param    Result the last failure code
* @return   result from set temperature range routine
*/
int tf_polaris10_thermal_set_temperature_range(struct pp_hwmgr *hwmgr,
		void *input, void *output, void *storage, int result)
{
	struct PP_TemperatureRange *range = (struct PP_TemperatureRange *)input;

	if (range == NULL)
		return -EINVAL;

	return polaris10_thermal_set_temperature_range(hwmgr, range->min, range->max);
}

/**
* Programs one-time setting registers
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput the pointer to input data
* @param    pOutput the pointer to output data
* @param    pStorage the pointer to temporary storage
* @param    Result the last failure code
* @return   result from initialize thermal controller routine
*/
int tf_polaris10_thermal_initialize(struct pp_hwmgr *hwmgr,
		void *input, void *output, void *storage, int result)
{
    return polaris10_thermal_initialize(hwmgr);
}

/**
* Enable high and low alerts
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput the pointer to input data
* @param    pOutput the pointer to output data
* @param    pStorage the pointer to temporary storage
* @param    Result the last failure code
* @return   result from enable alert routine
*/
int tf_polaris10_thermal_enable_alert(struct pp_hwmgr *hwmgr,
		void *input, void *output, void *storage, int result)
{
	return polaris10_thermal_enable_alert(hwmgr);
}

/**
* Disable high and low alerts
* @param    hwmgr  the address of the powerplay hardware manager.
* @param    pInput the pointer to input data
* @param    pOutput the pointer to output data
* @param    pStorage the pointer to temporary storage
* @param    Result the last failure code
* @return   result from disable alert routine
*/
static int tf_polaris10_thermal_disable_alert(struct pp_hwmgr *hwmgr,
		void *input, void *output, void *storage, int result)
{
	return polaris10_thermal_disable_alert(hwmgr);
}

static int tf_polaris10_thermal_avfs_enable(struct pp_hwmgr *hwmgr,
		void *input, void *output, void *storage, int result)
{
	int ret;
	struct pp_smumgr *smumgr = (struct pp_smumgr *)(hwmgr->smumgr);
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);

	if (smu_data->avfs.avfs_btc_status != AVFS_BTC_ENABLEAVFS)
		return 0;

	ret = (smum_send_msg_to_smc(smumgr, PPSMC_MSG_EnableAvfs) == 0) ?
			0 : -1;

	if (!ret)
		/* If this param is not changed, this function could fire unnecessarily */
		smu_data->avfs.avfs_btc_status = AVFS_BTC_COMPLETED_PREVIOUSLY;

	return ret;
}

static const struct phm_master_table_item
polaris10_thermal_start_thermal_controller_master_list[] = {
	{NULL, tf_polaris10_thermal_initialize},
	{NULL, tf_polaris10_thermal_set_temperature_range},
	{NULL, tf_polaris10_thermal_enable_alert},
	{NULL, tf_polaris10_thermal_avfs_enable},
/* We should restrict performance levels to low before we halt the SMC.
 * On the other hand we are still in boot state when we do this
 * so it would be pointless.
 * If this assumption changes we have to revisit this table.
 */
	{NULL, tf_polaris10_thermal_setup_fan_table},
	{NULL, tf_polaris10_thermal_start_smc_fan_control},
	{NULL, NULL}
};

static const struct phm_master_table_header
polaris10_thermal_start_thermal_controller_master = {
	0,
	PHM_MasterTableFlag_None,
	polaris10_thermal_start_thermal_controller_master_list
};

static const struct phm_master_table_item
polaris10_thermal_set_temperature_range_master_list[] = {
	{NULL, tf_polaris10_thermal_disable_alert},
	{NULL, tf_polaris10_thermal_set_temperature_range},
	{NULL, tf_polaris10_thermal_enable_alert},
	{NULL, NULL}
};

static const struct phm_master_table_header
polaris10_thermal_set_temperature_range_master = {
	0,
	PHM_MasterTableFlag_None,
	polaris10_thermal_set_temperature_range_master_list
};

int polaris10_thermal_ctrl_uninitialize_thermal_controller(struct pp_hwmgr *hwmgr)
{
	if (!hwmgr->thermal_controller.fanInfo.bNoFan)
		polaris10_fan_ctrl_set_default_mode(hwmgr);
	return 0;
}

/**
* Initializes the thermal controller related functions in the Hardware Manager structure.
* @param    hwmgr The address of the hardware manager.
* @exception Any error code from the low-level communication.
*/
int pp_polaris10_thermal_initialize(struct pp_hwmgr *hwmgr)
{
	int result;

	result = phm_construct_table(hwmgr,
			&polaris10_thermal_set_temperature_range_master,
			&(hwmgr->set_temperature_range));

	if (!result) {
		result = phm_construct_table(hwmgr,
				&polaris10_thermal_start_thermal_controller_master,
				&(hwmgr->start_thermal_controller));
		if (result)
			phm_destroy_table(hwmgr, &(hwmgr->set_temperature_range));
	}

	if (!result)
		hwmgr->fan_ctrl_is_in_default_mode = true;
	return result;
}

