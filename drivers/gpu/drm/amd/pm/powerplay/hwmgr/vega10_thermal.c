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

#include "vega10_thermal.h"
#include "vega10_hwmgr.h"
#include "vega10_smumgr.h"
#include "vega10_ppsmc.h"
#include "vega10_inc.h"
#include "soc15_common.h"
#include "pp_debug.h"

static int vega10_get_current_rpm(struct pp_hwmgr *hwmgr, uint32_t *current_rpm)
{
	smum_send_msg_to_smc(hwmgr, PPSMC_MSG_GetCurrentRpm, current_rpm);
	return 0;
}

int vega10_fan_ctrl_get_fan_speed_info(struct pp_hwmgr *hwmgr,
		struct phm_fan_speed_info *fan_speed_info)
{

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	fan_speed_info->supports_percent_read = true;
	fan_speed_info->supports_percent_write = true;
	fan_speed_info->min_percent = 0;
	fan_speed_info->max_percent = 100;

	if (PP_CAP(PHM_PlatformCaps_FanSpeedInTableIsRPM) &&
		hwmgr->thermal_controller.fanInfo.
		ucTachometerPulsesPerRevolution) {
		fan_speed_info->supports_rpm_read = true;
		fan_speed_info->supports_rpm_write = true;
		fan_speed_info->min_rpm =
				hwmgr->thermal_controller.fanInfo.ulMinRPM;
		fan_speed_info->max_rpm =
				hwmgr->thermal_controller.fanInfo.ulMaxRPM;
	} else {
		fan_speed_info->min_rpm = 0;
		fan_speed_info->max_rpm = 0;
	}

	return 0;
}

int vega10_fan_ctrl_get_fan_speed_percent(struct pp_hwmgr *hwmgr,
		uint32_t *speed)
{
	uint32_t current_rpm;
	uint32_t percent = 0;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	if (vega10_get_current_rpm(hwmgr, &current_rpm))
		return -1;

	if (hwmgr->thermal_controller.
			advanceFanControlParameters.usMaxFanRPM != 0)
		percent = current_rpm * 100 /
			hwmgr->thermal_controller.
			advanceFanControlParameters.usMaxFanRPM;

	*speed = percent > 100 ? 100 : percent;

	return 0;
}

int vega10_fan_ctrl_get_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t *speed)
{
	struct amdgpu_device *adev = hwmgr->adev;
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t tach_period;
	uint32_t crystal_clock_freq;
	int result = 0;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return -1;

	if (data->smu_features[GNLD_FAN_CONTROL].supported) {
		result = vega10_get_current_rpm(hwmgr, speed);
	} else {
		tach_period =
			REG_GET_FIELD(RREG32_SOC15(THM, 0, mmCG_TACH_STATUS),
					  CG_TACH_STATUS,
					  TACH_PERIOD);

		if (tach_period == 0)
			return -EINVAL;

		crystal_clock_freq = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);

		*speed = 60 * crystal_clock_freq * 10000 / tach_period;
	}

	return result;
}

/**
 * vega10_fan_ctrl_set_static_mode - Set Fan Speed Control to static mode,
 * so that the user can decide what speed to use.
 * @hwmgr:  the address of the powerplay hardware manager.
 * @mode: the fan control mode, 0 default, 1 by percent, 5, by RPM
 * Exception: Should always succeed.
 */
int vega10_fan_ctrl_set_static_mode(struct pp_hwmgr *hwmgr, uint32_t mode)
{
	struct amdgpu_device *adev = hwmgr->adev;

	if (hwmgr->fan_ctrl_is_in_default_mode) {
		hwmgr->fan_ctrl_default_mode =
			REG_GET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				CG_FDO_CTRL2, FDO_PWM_MODE);
		hwmgr->tmin =
			REG_GET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				CG_FDO_CTRL2, TMIN);
		hwmgr->fan_ctrl_is_in_default_mode = false;
	}

	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
			REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				CG_FDO_CTRL2, TMIN, 0));
	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
			REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				CG_FDO_CTRL2, FDO_PWM_MODE, mode));

	return 0;
}

/**
 * vega10_fan_ctrl_set_default_mode - Reset Fan Speed Control to default mode.
 * @hwmgr:  the address of the powerplay hardware manager.
 * Exception: Should always succeed.
 */
int vega10_fan_ctrl_set_default_mode(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	if (!hwmgr->fan_ctrl_is_in_default_mode) {
		WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
			REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				CG_FDO_CTRL2, FDO_PWM_MODE,
				hwmgr->fan_ctrl_default_mode));
		WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
			REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
				CG_FDO_CTRL2, TMIN,
				hwmgr->tmin << CG_FDO_CTRL2__TMIN__SHIFT));
		hwmgr->fan_ctrl_is_in_default_mode = true;
	}

	return 0;
}

/**
 * vega10_enable_fan_control_feature - Enables the SMC Fan Control Feature.
 *
 * @hwmgr: the address of the powerplay hardware manager.
 * Return:   0 on success. -1 otherwise.
 */
static int vega10_enable_fan_control_feature(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_FAN_CONTROL].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(
				hwmgr, true,
				data->smu_features[GNLD_FAN_CONTROL].
				smu_feature_bitmap),
				"Attempt to Enable FAN CONTROL feature Failed!",
				return -1);
		data->smu_features[GNLD_FAN_CONTROL].enabled = true;
	}

	return 0;
}

static int vega10_disable_fan_control_feature(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_FAN_CONTROL].supported) {
		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(
				hwmgr, false,
				data->smu_features[GNLD_FAN_CONTROL].
				smu_feature_bitmap),
				"Attempt to Enable FAN CONTROL feature Failed!",
				return -1);
		data->smu_features[GNLD_FAN_CONTROL].enabled = false;
	}

	return 0;
}

int vega10_fan_ctrl_start_smc_fan_control(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return -1;

	PP_ASSERT_WITH_CODE(!vega10_enable_fan_control_feature(hwmgr),
			"Attempt to Enable SMC FAN CONTROL Feature Failed!",
			return -1);

	return 0;
}


int vega10_fan_ctrl_stop_smc_fan_control(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return -1;

	if (data->smu_features[GNLD_FAN_CONTROL].supported) {
		PP_ASSERT_WITH_CODE(!vega10_disable_fan_control_feature(hwmgr),
				"Attempt to Disable SMC FAN CONTROL Feature Failed!",
				return -1);
	}
	return 0;
}

/**
 * vega10_fan_ctrl_set_fan_speed_percent - Set Fan Speed in percent.
 * @hwmgr:  the address of the powerplay hardware manager.
 * @speed: is the percentage value (0% - 100%) to be set.
 * Exception: Fails is the 100% setting appears to be 0.
 */
int vega10_fan_ctrl_set_fan_speed_percent(struct pp_hwmgr *hwmgr,
		uint32_t speed)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t duty100;
	uint32_t duty;
	uint64_t tmp64;

	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	if (speed > 100)
		speed = 100;

	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
		vega10_fan_ctrl_stop_smc_fan_control(hwmgr);

	duty100 = REG_GET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL1),
				    CG_FDO_CTRL1, FMAX_DUTY100);

	if (duty100 == 0)
		return -EINVAL;

	tmp64 = (uint64_t)speed * duty100;
	do_div(tmp64, 100);
	duty = (uint32_t)tmp64;

	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL0,
		REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL0),
			CG_FDO_CTRL0, FDO_STATIC_DUTY, duty));

	return vega10_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC);
}

/**
 * vega10_fan_ctrl_reset_fan_speed_to_default - Reset Fan Speed to default.
 * @hwmgr:  the address of the powerplay hardware manager.
 * Exception: Always succeeds.
 */
int vega10_fan_ctrl_reset_fan_speed_to_default(struct pp_hwmgr *hwmgr)
{
	if (hwmgr->thermal_controller.fanInfo.bNoFan)
		return 0;

	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
		return vega10_fan_ctrl_start_smc_fan_control(hwmgr);
	else
		return vega10_fan_ctrl_set_default_mode(hwmgr);
}

/**
 * vega10_fan_ctrl_set_fan_speed_rpm - Set Fan Speed in RPM.
 * @hwmgr:  the address of the powerplay hardware manager.
 * @speed: is the percentage value (min - max) to be set.
 * Exception: Fails is the speed not lie between min and max.
 */
int vega10_fan_ctrl_set_fan_speed_rpm(struct pp_hwmgr *hwmgr, uint32_t speed)
{
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t tach_period;
	uint32_t crystal_clock_freq;
	int result = 0;

	if (hwmgr->thermal_controller.fanInfo.bNoFan ||
	    speed == 0 ||
	    (speed < hwmgr->thermal_controller.fanInfo.ulMinRPM) ||
	    (speed > hwmgr->thermal_controller.fanInfo.ulMaxRPM))
		return -1;

	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
		result = vega10_fan_ctrl_stop_smc_fan_control(hwmgr);

	if (!result) {
		crystal_clock_freq = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);
		tach_period = 60 * crystal_clock_freq * 10000 / (8 * speed);
		WREG32_SOC15(THM, 0, mmCG_TACH_CTRL,
				REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_TACH_CTRL),
					CG_TACH_CTRL, TARGET_PERIOD,
					tach_period));
	}
	return vega10_fan_ctrl_set_static_mode(hwmgr, FDO_PWM_MODE_STATIC_RPM);
}

/**
 * vega10_thermal_get_temperature - Reads the remote temperature from the SIslands thermal controller.
 *
 * @hwmgr: The address of the hardware manager.
 */
int vega10_thermal_get_temperature(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	int temp;

	temp = RREG32_SOC15(THM, 0, mmCG_MULT_THERMAL_STATUS);

	temp = (temp & CG_MULT_THERMAL_STATUS__CTF_TEMP_MASK) >>
			CG_MULT_THERMAL_STATUS__CTF_TEMP__SHIFT;

	temp = temp & 0x1ff;

	temp *= PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return temp;
}

/**
 * vega10_thermal_set_temperature_range - Set the requested temperature range for high and low alert signals
 *
 * @hwmgr: The address of the hardware manager.
 * @range: Temperature range to be programmed for
 *           high and low alert signals
 * Exception: PP_Result_BadInput if the input data is not valid.
 */
static int vega10_thermal_set_temperature_range(struct pp_hwmgr *hwmgr,
		struct PP_TemperatureRange *range)
{
	struct phm_ppt_v2_information *pp_table_info =
		(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_tdp_table *tdp_table = pp_table_info->tdp_table;
	struct amdgpu_device *adev = hwmgr->adev;
	int low = VEGA10_THERMAL_MINIMUM_ALERT_TEMP;
	int high = VEGA10_THERMAL_MAXIMUM_ALERT_TEMP;
	uint32_t val;

	/* compare them in unit celsius degree */
	if (low < range->min / PP_TEMPERATURE_UNITS_PER_CENTIGRADES)
		low = range->min / PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	/*
	 * As a common sense, usSoftwareShutdownTemp should be bigger
	 * than ThotspotLimit. For any invalid usSoftwareShutdownTemp,
	 * we will just use the max possible setting VEGA10_THERMAL_MAXIMUM_ALERT_TEMP
	 * to avoid false alarms.
	 */
	if ((tdp_table->usSoftwareShutdownTemp >
	     range->hotspot_crit_max / PP_TEMPERATURE_UNITS_PER_CENTIGRADES)) {
		if (high > tdp_table->usSoftwareShutdownTemp)
			high = tdp_table->usSoftwareShutdownTemp;
	}

	if (low > high)
		return -EINVAL;

	val = RREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL);

	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, MAX_IH_CREDIT, 5);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, THERM_IH_HW_ENA, 1);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTH, high);
	val = REG_SET_FIELD(val, THM_THERMAL_INT_CTRL, DIG_THERM_INTL, low);
	val &= (~THM_THERMAL_INT_CTRL__THERM_TRIGGER_MASK_MASK) &
			(~THM_THERMAL_INT_CTRL__THERM_INTH_MASK_MASK) &
			(~THM_THERMAL_INT_CTRL__THERM_INTL_MASK_MASK);

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_CTRL, val);

	return 0;
}

/**
 * vega10_thermal_initialize - Programs thermal controller one-time setting registers
 *
 * @hwmgr: The address of the hardware manager.
 */
static int vega10_thermal_initialize(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;

	if (hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution) {
		WREG32_SOC15(THM, 0, mmCG_TACH_CTRL,
			REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_TACH_CTRL),
				CG_TACH_CTRL, EDGE_PER_REV,
				hwmgr->thermal_controller.fanInfo.ucTachometerPulsesPerRevolution - 1));
	}

	WREG32_SOC15(THM, 0, mmCG_FDO_CTRL2,
		REG_SET_FIELD(RREG32_SOC15(THM, 0, mmCG_FDO_CTRL2),
			CG_FDO_CTRL2, TACH_PWM_RESP_RATE, 0x28));

	return 0;
}

/**
 * vega10_thermal_enable_alert - Enable thermal alerts on the RV770 thermal controller.
 *
 * @hwmgr: The address of the hardware manager.
 */
static int vega10_thermal_enable_alert(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	struct vega10_hwmgr *data = hwmgr->backend;
	uint32_t val = 0;

	if (data->smu_features[GNLD_FW_CTF].supported) {
		if (data->smu_features[GNLD_FW_CTF].enabled)
			printk("[Thermal_EnableAlert] FW CTF Already Enabled!\n");

		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
				true,
				data->smu_features[GNLD_FW_CTF].smu_feature_bitmap),
				"Attempt to Enable FW CTF feature Failed!",
				return -1);
		data->smu_features[GNLD_FW_CTF].enabled = true;
	}

	val |= (1 << THM_THERMAL_INT_ENA__THERM_INTH_CLR__SHIFT);
	val |= (1 << THM_THERMAL_INT_ENA__THERM_INTL_CLR__SHIFT);
	val |= (1 << THM_THERMAL_INT_ENA__THERM_TRIGGER_CLR__SHIFT);

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, val);

	return 0;
}

/**
 * vega10_thermal_disable_alert - Disable thermal alerts on the RV770 thermal controller.
 * @hwmgr: The address of the hardware manager.
 */
int vega10_thermal_disable_alert(struct pp_hwmgr *hwmgr)
{
	struct amdgpu_device *adev = hwmgr->adev;
	struct vega10_hwmgr *data = hwmgr->backend;

	if (data->smu_features[GNLD_FW_CTF].supported) {
		if (!data->smu_features[GNLD_FW_CTF].enabled)
			printk("[Thermal_EnableAlert] FW CTF Already disabled!\n");


		PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr,
			false,
			data->smu_features[GNLD_FW_CTF].smu_feature_bitmap),
			"Attempt to disable FW CTF feature Failed!",
			return -1);
		data->smu_features[GNLD_FW_CTF].enabled = false;
	}

	WREG32_SOC15(THM, 0, mmTHM_THERMAL_INT_ENA, 0);

	return 0;
}

/**
 * vega10_thermal_stop_thermal_controller - Uninitialize the thermal controller.
 * Currently just disables alerts.
 * @hwmgr: The address of the hardware manager.
 */
int vega10_thermal_stop_thermal_controller(struct pp_hwmgr *hwmgr)
{
	int result = vega10_thermal_disable_alert(hwmgr);

	if (!hwmgr->thermal_controller.fanInfo.bNoFan)
		vega10_fan_ctrl_set_default_mode(hwmgr);

	return result;
}

/**
 * vega10_thermal_setup_fan_table - Set up the fan table to control the fan using the SMC.
 * @hwmgr:  the address of the powerplay hardware manager.
 * Return:   result from set temperature range routine
 */
static int vega10_thermal_setup_fan_table(struct pp_hwmgr *hwmgr)
{
	int ret;
	struct vega10_hwmgr *data = hwmgr->backend;
	PPTable_t *table = &(data->smc_state_table.pp_table);

	if (!data->smu_features[GNLD_FAN_CONTROL].supported)
		return 0;

	table->FanMaximumRpm = (uint16_t)hwmgr->thermal_controller.
			advanceFanControlParameters.usMaxFanRPM;
	table->FanThrottlingRpm = hwmgr->thermal_controller.
			advanceFanControlParameters.usFanRPMMaxLimit;
	table->FanAcousticLimitRpm = (uint16_t)(hwmgr->thermal_controller.
			advanceFanControlParameters.ulMinFanSCLKAcousticLimit);
	table->FanTargetTemperature = hwmgr->thermal_controller.
			advanceFanControlParameters.usTMax;

	smum_send_msg_to_smc_with_parameter(hwmgr,
				PPSMC_MSG_SetFanTemperatureTarget,
				(uint32_t)table->FanTargetTemperature,
				NULL);

	table->FanPwmMin = hwmgr->thermal_controller.
			advanceFanControlParameters.usPWMMin * 255 / 100;
	table->FanTargetGfxclk = (uint16_t)(hwmgr->thermal_controller.
			advanceFanControlParameters.ulTargetGfxClk);
	table->FanGainEdge = hwmgr->thermal_controller.
			advanceFanControlParameters.usFanGainEdge;
	table->FanGainHotspot = hwmgr->thermal_controller.
			advanceFanControlParameters.usFanGainHotspot;
	table->FanGainLiquid = hwmgr->thermal_controller.
			advanceFanControlParameters.usFanGainLiquid;
	table->FanGainVrVddc = hwmgr->thermal_controller.
			advanceFanControlParameters.usFanGainVrVddc;
	table->FanGainVrMvdd = hwmgr->thermal_controller.
			advanceFanControlParameters.usFanGainVrMvdd;
	table->FanGainPlx = hwmgr->thermal_controller.
			advanceFanControlParameters.usFanGainPlx;
	table->FanGainHbm = hwmgr->thermal_controller.
			advanceFanControlParameters.usFanGainHbm;
	table->FanZeroRpmEnable = hwmgr->thermal_controller.
			advanceFanControlParameters.ucEnableZeroRPM;
	table->FanStopTemp = hwmgr->thermal_controller.
			advanceFanControlParameters.usZeroRPMStopTemperature;
	table->FanStartTemp = hwmgr->thermal_controller.
			advanceFanControlParameters.usZeroRPMStartTemperature;

	ret = smum_smc_table_manager(hwmgr,
				(uint8_t *)(&(data->smc_state_table.pp_table)),
				PPTABLE, false);
	if (ret)
		pr_info("Failed to update Fan Control Table in PPTable!");

	return ret;
}

int vega10_enable_mgpu_fan_boost(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = hwmgr->backend;
	PPTable_t *table = &(data->smc_state_table.pp_table);
	int ret;

	if (!data->smu_features[GNLD_FAN_CONTROL].supported)
		return 0;

	if (!hwmgr->thermal_controller.advanceFanControlParameters.
			usMGpuThrottlingRPMLimit)
		return 0;

	table->FanThrottlingRpm = hwmgr->thermal_controller.
			advanceFanControlParameters.usMGpuThrottlingRPMLimit;

	ret = smum_smc_table_manager(hwmgr,
				(uint8_t *)(&(data->smc_state_table.pp_table)),
				PPTABLE, false);
	if (ret) {
		pr_info("Failed to update fan control table in pptable!");
		return ret;
	}

	ret = vega10_disable_fan_control_feature(hwmgr);
	if (ret) {
		pr_info("Attempt to disable SMC fan control feature failed!");
		return ret;
	}

	ret = vega10_enable_fan_control_feature(hwmgr);
	if (ret)
		pr_info("Attempt to enable SMC fan control feature failed!");

	return ret;
}

/**
 * vega10_thermal_start_smc_fan_control - Start the fan control on the SMC.
 * @hwmgr:  the address of the powerplay hardware manager.
 * Return:   result from set temperature range routine
 */
static int vega10_thermal_start_smc_fan_control(struct pp_hwmgr *hwmgr)
{
/* If the fantable setup has failed we could have disabled
 * PHM_PlatformCaps_MicrocodeFanControl even after
 * this function was included in the table.
 * Make sure that we still think controlling the fan is OK.
*/
	if (PP_CAP(PHM_PlatformCaps_MicrocodeFanControl))
		vega10_fan_ctrl_start_smc_fan_control(hwmgr);

	return 0;
}


int vega10_start_thermal_controller(struct pp_hwmgr *hwmgr,
				struct PP_TemperatureRange *range)
{
	int ret = 0;

	if (range == NULL)
		return -EINVAL;

	vega10_thermal_initialize(hwmgr);
	ret = vega10_thermal_set_temperature_range(hwmgr, range);
	if (ret)
		return -EINVAL;

	vega10_thermal_enable_alert(hwmgr);
/* We should restrict performance levels to low before we halt the SMC.
 * On the other hand we are still in boot state when we do this
 * so it would be pointless.
 * If this assumption changes we have to revisit this table.
 */
	ret = vega10_thermal_setup_fan_table(hwmgr);
	if (ret)
		return -EINVAL;

	vega10_thermal_start_smc_fan_control(hwmgr);

	return 0;
};




int vega10_thermal_ctrl_uninitialize_thermal_controller(struct pp_hwmgr *hwmgr)
{
	if (!hwmgr->thermal_controller.fanInfo.bNoFan) {
		vega10_fan_ctrl_set_default_mode(hwmgr);
		vega10_fan_ctrl_stop_smc_fan_control(hwmgr);
	}
	return 0;
}
