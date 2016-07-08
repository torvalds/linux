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

#include "hwmgr.h"
#include "smumgr.h"
#include "fiji_hwmgr.h"
#include "fiji_powertune.h"
#include "fiji_smumgr.h"
#include "smu73_discrete.h"
#include "pp_debug.h"

#define VOLTAGE_SCALE  4
#define POWERTUNE_DEFAULT_SET_MAX    1

const struct fiji_pt_defaults fiji_power_tune_data_set_array[POWERTUNE_DEFAULT_SET_MAX] = {
		/*sviLoadLIneEn,  SviLoadLineVddC, TDC_VDDC_ThrottleReleaseLimitPerc */
		{1,               0xF,             0xFD,
		/* TDC_MAWt, TdcWaterfallCtl, DTEAmbientTempBase */
		0x19,        5,               45}
};

void fiji_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *fiji_hwmgr = (struct fiji_hwmgr *)(hwmgr->backend);
	struct  phm_ppt_v1_information *table_info =
			(struct  phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t tmp = 0;

	if(table_info &&
			table_info->cac_dtp_table->usPowerTuneDataSetID <= POWERTUNE_DEFAULT_SET_MAX &&
			table_info->cac_dtp_table->usPowerTuneDataSetID)
		fiji_hwmgr->power_tune_defaults =
				&fiji_power_tune_data_set_array
				[table_info->cac_dtp_table->usPowerTuneDataSetID - 1];
	else
		fiji_hwmgr->power_tune_defaults = &fiji_power_tune_data_set_array[0];

	/* Assume disabled */
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SQRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_DBRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TDRamping);
	phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_TCPRamping);

	fiji_hwmgr->dte_tj_offset = tmp;

	if (!tmp) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_CAC);

		fiji_hwmgr->fast_watermark_threshold = 100;

		if (hwmgr->powercontainment_enabled) {
			phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				    PHM_PlatformCaps_PowerContainment);
			tmp = 1;
			fiji_hwmgr->enable_dte_feature = tmp ? false : true;
			fiji_hwmgr->enable_tdc_limit_feature = tmp ? true : false;
			fiji_hwmgr->enable_pkg_pwr_tracking_feature = tmp ? true : false;
		}
	}
}

/* PPGen has the gain setting generated in x * 100 unit
 * This function is to convert the unit to x * 4096(0x1000) unit.
 *  This is the unit expected by SMC firmware
 */
static uint16_t scale_fan_gain_settings(uint16_t raw_setting)
{
	uint32_t tmp;
	tmp = raw_setting * 4096 / 100;
	return (uint16_t)tmp;
}

static void get_scl_sda_value(uint8_t line, uint8_t *scl, uint8_t* sda)
{
	switch (line) {
	case Fiji_I2CLineID_DDC1 :
		*scl = Fiji_I2C_DDC1CLK;
		*sda = Fiji_I2C_DDC1DATA;
		break;
	case Fiji_I2CLineID_DDC2 :
		*scl = Fiji_I2C_DDC2CLK;
		*sda = Fiji_I2C_DDC2DATA;
		break;
	case Fiji_I2CLineID_DDC3 :
		*scl = Fiji_I2C_DDC3CLK;
		*sda = Fiji_I2C_DDC3DATA;
		break;
	case Fiji_I2CLineID_DDC4 :
		*scl = Fiji_I2C_DDC4CLK;
		*sda = Fiji_I2C_DDC4DATA;
		break;
	case Fiji_I2CLineID_DDC5 :
		*scl = Fiji_I2C_DDC5CLK;
		*sda = Fiji_I2C_DDC5DATA;
		break;
	case Fiji_I2CLineID_DDC6 :
		*scl = Fiji_I2C_DDC6CLK;
		*sda = Fiji_I2C_DDC6DATA;
		break;
	case Fiji_I2CLineID_SCLSDA :
		*scl = Fiji_I2C_SCL;
		*sda = Fiji_I2C_SDA;
		break;
	case Fiji_I2CLineID_DDCVGA :
		*scl = Fiji_I2C_DDCVGACLK;
		*sda = Fiji_I2C_DDCVGADATA;
		break;
	default:
		*scl = 0;
		*sda = 0;
		break;
	}
}

int fiji_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	const struct fiji_pt_defaults *defaults = data->power_tune_defaults;
	SMU73_Discrete_DpmTable  *dpm_table = &(data->smc_state_table);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_cac_tdp_table *cac_dtp_table = table_info->cac_dtp_table;
	struct pp_advance_fan_control_parameters *fan_table=
			&hwmgr->thermal_controller.advanceFanControlParameters;
	uint8_t uc_scl, uc_sda;

	/* TDP number of fraction bits are changed from 8 to 7 for Fiji
	 * as requested by SMC team
	 */
	dpm_table->DefaultTdp = PP_HOST_TO_SMC_US(
			(uint16_t)(cac_dtp_table->usTDP * 128));
	dpm_table->TargetTdp = PP_HOST_TO_SMC_US(
			(uint16_t)(cac_dtp_table->usTDP * 128));

	PP_ASSERT_WITH_CODE(cac_dtp_table->usTargetOperatingTemp <= 255,
			"Target Operating Temp is out of Range!",);

	dpm_table->GpuTjMax = (uint8_t)(cac_dtp_table->usTargetOperatingTemp);
	dpm_table->GpuTjHyst = 8;

	dpm_table->DTEAmbientTempBase = defaults->DTEAmbientTempBase;

	/* The following are for new Fiji Multi-input fan/thermal control */
	dpm_table->TemperatureLimitEdge = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTargetOperatingTemp * 256);
	dpm_table->TemperatureLimitHotspot = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTemperatureLimitHotspot * 256);
	dpm_table->TemperatureLimitLiquid1 = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTemperatureLimitLiquid1 * 256);
	dpm_table->TemperatureLimitLiquid2 = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTemperatureLimitLiquid2 * 256);
	dpm_table->TemperatureLimitVrVddc = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTemperatureLimitVrVddc * 256);
	dpm_table->TemperatureLimitVrMvdd = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTemperatureLimitVrMvdd * 256);
	dpm_table->TemperatureLimitPlx = PP_HOST_TO_SMC_US(
			cac_dtp_table->usTemperatureLimitPlx * 256);

	dpm_table->FanGainEdge = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainEdge));
	dpm_table->FanGainHotspot = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainHotspot));
	dpm_table->FanGainLiquid = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainLiquid));
	dpm_table->FanGainVrVddc = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainVrVddc));
	dpm_table->FanGainVrMvdd = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainVrMvdd));
	dpm_table->FanGainPlx = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainPlx));
	dpm_table->FanGainHbm = PP_HOST_TO_SMC_US(
			scale_fan_gain_settings(fan_table->usFanGainHbm));

	dpm_table->Liquid1_I2C_address = cac_dtp_table->ucLiquid1_I2C_address;
	dpm_table->Liquid2_I2C_address = cac_dtp_table->ucLiquid2_I2C_address;
	dpm_table->Vr_I2C_address = cac_dtp_table->ucVr_I2C_address;
	dpm_table->Plx_I2C_address = cac_dtp_table->ucPlx_I2C_address;

	get_scl_sda_value(cac_dtp_table->ucLiquid_I2C_Line, &uc_scl, &uc_sda);
	dpm_table->Liquid_I2C_LineSCL = uc_scl;
	dpm_table->Liquid_I2C_LineSDA = uc_sda;

	get_scl_sda_value(cac_dtp_table->ucVr_I2C_Line, &uc_scl, &uc_sda);
	dpm_table->Vr_I2C_LineSCL = uc_scl;
	dpm_table->Vr_I2C_LineSDA = uc_sda;

	get_scl_sda_value(cac_dtp_table->ucPlx_I2C_Line, &uc_scl, &uc_sda);
	dpm_table->Plx_I2C_LineSCL = uc_scl;
	dpm_table->Plx_I2C_LineSDA = uc_sda;

	return 0;
}

static int fiji_populate_svi_load_line(struct pp_hwmgr *hwmgr)
{
    struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
    const struct fiji_pt_defaults *defaults = data->power_tune_defaults;

    data->power_tune_table.SviLoadLineEn = defaults->SviLoadLineEn;
    data->power_tune_table.SviLoadLineVddC = defaults->SviLoadLineVddC;
    data->power_tune_table.SviLoadLineTrimVddC = 3;
    data->power_tune_table.SviLoadLineOffsetVddC = 0;

    return 0;
}

static int fiji_populate_tdc_limit(struct pp_hwmgr *hwmgr)
{
	uint16_t tdc_limit;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	const struct fiji_pt_defaults *defaults = data->power_tune_defaults;

	/* TDC number of fraction bits are changed from 8 to 7
	 * for Fiji as requested by SMC team
	 */
	tdc_limit = (uint16_t)(table_info->cac_dtp_table->usTDC * 128);
	data->power_tune_table.TDC_VDDC_PkgLimit =
			CONVERT_FROM_HOST_TO_SMC_US(tdc_limit);
	data->power_tune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
			defaults->TDC_VDDC_ThrottleReleaseLimitPerc;
	data->power_tune_table.TDC_MAWt = defaults->TDC_MAWt;

	return 0;
}

static int fiji_populate_dw8(struct pp_hwmgr *hwmgr, uint32_t fuse_table_offset)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	const struct fiji_pt_defaults *defaults = data->power_tune_defaults;
	uint32_t temp;

	if (fiji_read_smc_sram_dword(hwmgr->smumgr,
			fuse_table_offset +
			offsetof(SMU73_Discrete_PmFuses, TdcWaterfallCtl),
			(uint32_t *)&temp, data->sram_end))
		PP_ASSERT_WITH_CODE(false,
				"Attempt to read PmFuses.DW6 (SviLoadLineEn) from SMC Failed!",
				return -EINVAL);
	else {
		data->power_tune_table.TdcWaterfallCtl = defaults->TdcWaterfallCtl;
		data->power_tune_table.LPMLTemperatureMin =
				(uint8_t)((temp >> 16) & 0xff);
		data->power_tune_table.LPMLTemperatureMax =
				(uint8_t)((temp >> 8) & 0xff);
		data->power_tune_table.Reserved = (uint8_t)(temp & 0xff);
	}
	return 0;
}

static int fiji_populate_temperature_scaler(struct pp_hwmgr *hwmgr)
{
	int i;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		data->power_tune_table.LPMLTemperatureScaler[i] = 0;

	return 0;
}

static int fiji_populate_fuzzy_fan(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if( (hwmgr->thermal_controller.advanceFanControlParameters.
			usFanOutputSensitivity & (1 << 15)) ||
			0 == hwmgr->thermal_controller.advanceFanControlParameters.
			usFanOutputSensitivity )
		hwmgr->thermal_controller.advanceFanControlParameters.
		usFanOutputSensitivity = hwmgr->thermal_controller.
			advanceFanControlParameters.usDefaultFanOutputSensitivity;

	data->power_tune_table.FuzzyFan_PwmSetDelta =
			PP_HOST_TO_SMC_US(hwmgr->thermal_controller.
					advanceFanControlParameters.usFanOutputSensitivity);
	return 0;
}

static int fiji_populate_gnb_lpml(struct pp_hwmgr *hwmgr)
{
	int i;
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		data->power_tune_table.GnbLPML[i] = 0;

	return 0;
}

static int fiji_min_max_vgnb_lpml_id_from_bapm_vddc(struct pp_hwmgr *hwmgr)
{
 /*   int  i, min, max;
    struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
    uint8_t * pHiVID = data->power_tune_table.BapmVddCVidHiSidd;
    uint8_t * pLoVID = data->power_tune_table.BapmVddCVidLoSidd;

    min = max = pHiVID[0];
    for (i = 0; i < 8; i++) {
        if (0 != pHiVID[i]) {
            if (min > pHiVID[i])
                min = pHiVID[i];
            if (max < pHiVID[i])
                max = pHiVID[i];
        }

        if (0 != pLoVID[i]) {
            if (min > pLoVID[i])
                min = pLoVID[i];
            if (max < pLoVID[i])
                max = pLoVID[i];
        }
    }

    PP_ASSERT_WITH_CODE((0 != min) && (0 != max), "BapmVddcVidSidd table does not exist!", return int_Failed);
    data->power_tune_table.GnbLPMLMaxVid = (uint8_t)max;
    data->power_tune_table.GnbLPMLMinVid = (uint8_t)min;
*/
    return 0;
}

static int fiji_populate_bapm_vddc_base_leakage_sidd(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint16_t HiSidd = data->power_tune_table.BapmVddCBaseLeakageHiSidd;
	uint16_t LoSidd = data->power_tune_table.BapmVddCBaseLeakageLoSidd;
	struct phm_cac_tdp_table *cac_table = table_info->cac_dtp_table;

	HiSidd = (uint16_t)(cac_table->usHighCACLeakage / 100 * 256);
	LoSidd = (uint16_t)(cac_table->usLowCACLeakage / 100 * 256);

	data->power_tune_table.BapmVddCBaseLeakageHiSidd =
			CONVERT_FROM_HOST_TO_SMC_US(HiSidd);
	data->power_tune_table.BapmVddCBaseLeakageLoSidd =
			CONVERT_FROM_HOST_TO_SMC_US(LoSidd);

	return 0;
}

int fiji_populate_pm_fuses(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	uint32_t pm_fuse_table_offset;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (fiji_read_smc_sram_dword(hwmgr->smumgr,
				SMU7_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU73_Firmware_Header, PmFuseTable),
				&pm_fuse_table_offset, data->sram_end))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to get pm_fuse_table_offset Failed!",
					return -EINVAL);

		/* DW6 */
		if (fiji_populate_svi_load_line(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate SviLoadLine Failed!",
					return -EINVAL);
		/* DW7 */
		if (fiji_populate_tdc_limit(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TDCLimit Failed!", return -EINVAL);
		/* DW8 */
		if (fiji_populate_dw8(hwmgr, pm_fuse_table_offset))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TdcWaterfallCtl, "
					"LPMLTemperature Min and Max Failed!",
					return -EINVAL);

		/* DW9-DW12 */
		if (0 != fiji_populate_temperature_scaler(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate LPMLTemperatureScaler Failed!",
					return -EINVAL);

		/* DW13-DW14 */
		if(fiji_populate_fuzzy_fan(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate Fuzzy Fan Control parameters Failed!",
					return -EINVAL);

		/* DW15-DW18 */
		if (fiji_populate_gnb_lpml(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Failed!",
					return -EINVAL);

		/* DW19 */
		if (fiji_min_max_vgnb_lpml_id_from_bapm_vddc(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Min and Max Vid Failed!",
					return -EINVAL);

		/* DW20 */
		if (fiji_populate_bapm_vddc_base_leakage_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate BapmVddCBaseLeakage Hi and Lo "
					"Sidd Failed!", return -EINVAL);

		if (fiji_copy_bytes_to_smc(hwmgr->smumgr, pm_fuse_table_offset,
				(uint8_t *)&data->power_tune_table,
				sizeof(struct SMU73_Discrete_PmFuses), data->sram_end))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to download PmFuseTable Failed!",
					return -EINVAL);
	}
	return 0;
}

int fiji_enable_smc_cac(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC)) {
		int smc_result;
		smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
				(uint16_t)(PPSMC_MSG_EnableCac));
		PP_ASSERT_WITH_CODE((0 == smc_result),
				"Failed to enable CAC in SMC.", result = -1);

        data->cac_enabled = (0 == smc_result) ? true : false;
	}
	return result;
}

int fiji_disable_smc_cac(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC) && data->cac_enabled) {
		int smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
				(uint16_t)(PPSMC_MSG_DisableCac));
		PP_ASSERT_WITH_CODE((smc_result == 0),
				"Failed to disable CAC in SMC.", result = -1);

		data->cac_enabled = false;
	}
	return result;
}

int fiji_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);

	if(data->power_containment_features &
			POWERCONTAINMENT_FEATURE_PkgPwrLimit)
		return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_PkgPwrSetLimit, n);
	return 0;
}

static int fiji_set_overdriver_target_tdp(struct pp_hwmgr *pHwMgr, uint32_t target_tdp)
{
	return smum_send_msg_to_smc_with_parameter(pHwMgr->smumgr,
			PPSMC_MSG_OverDriveSetTargetTdp, target_tdp);
}

int fiji_enable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	int smc_result;
	int result = 0;

	data->power_containment_features = 0;
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (data->enable_dte_feature) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_EnableDTE));
			PP_ASSERT_WITH_CODE((0 == smc_result),
					"Failed to enable DTE in SMC.", result = -1;);
			if (0 == smc_result)
				data->power_containment_features |= POWERCONTAINMENT_FEATURE_DTE;
		}

		if (data->enable_tdc_limit_feature) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_TDCLimitEnable));
			PP_ASSERT_WITH_CODE((0 == smc_result),
					"Failed to enable TDCLimit in SMC.", result = -1;);
			if (0 == smc_result)
				data->power_containment_features |=
						POWERCONTAINMENT_FEATURE_TDCLimit;
		}

		if (data->enable_pkg_pwr_tracking_feature) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_PkgPwrLimitEnable));
			PP_ASSERT_WITH_CODE((0 == smc_result),
					"Failed to enable PkgPwrTracking in SMC.", result = -1;);
			if (0 == smc_result) {
				struct phm_cac_tdp_table *cac_table =
						table_info->cac_dtp_table;
				uint32_t default_limit =
					(uint32_t)(cac_table->usMaximumPowerDeliveryLimit * 256);

				data->power_containment_features |=
						POWERCONTAINMENT_FEATURE_PkgPwrLimit;

				if (fiji_set_power_limit(hwmgr, default_limit))
					printk(KERN_ERR "Failed to set Default Power Limit in SMC!");
			}
		}
	}
	return result;
}

int fiji_disable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct fiji_hwmgr *data = (struct fiji_hwmgr *)(hwmgr->backend);
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment) &&
			data->power_containment_features) {
		int smc_result;

		if (data->power_containment_features &
				POWERCONTAINMENT_FEATURE_TDCLimit) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_TDCLimitDisable));
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to disable TDCLimit in SMC.",
					result = smc_result);
		}

		if (data->power_containment_features &
				POWERCONTAINMENT_FEATURE_DTE) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_DisableDTE));
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to disable DTE in SMC.",
					result = smc_result);
		}

		if (data->power_containment_features &
				POWERCONTAINMENT_FEATURE_PkgPwrLimit) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_PkgPwrLimitDisable));
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to disable PkgPwrTracking in SMC.",
					result = smc_result);
		}
		data->power_containment_features = 0;
	}

	return result;
}

int fiji_power_control_set_level(struct pp_hwmgr *hwmgr)
{
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_cac_tdp_table *cac_table = table_info->cac_dtp_table;
	int adjust_percent, target_tdp;
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		/* adjustment percentage has already been validated */
		adjust_percent = hwmgr->platform_descriptor.TDPAdjustmentPolarity ?
				hwmgr->platform_descriptor.TDPAdjustment :
				(-1 * hwmgr->platform_descriptor.TDPAdjustment);
		/* SMC requested that target_tdp to be 7 bit fraction in DPM table
		 * but message to be 8 bit fraction for messages
		 */
		target_tdp = ((100 + adjust_percent) * (int)(cac_table->usTDP * 256)) / 100;
		result = fiji_set_overdriver_target_tdp(hwmgr, (uint32_t)target_tdp);
	}

	return result;
}
