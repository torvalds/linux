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
#include "tonga_hwmgr.h"
#include "tonga_powertune.h"
#include "tonga_smumgr.h"
#include "smu72_discrete.h"
#include "pp_debug.h"
#include "tonga_ppsmc.h"

#define VOLTAGE_SCALE  4
#define POWERTUNE_DEFAULT_SET_MAX    1

struct tonga_pt_defaults tonga_power_tune_data_set_array[POWERTUNE_DEFAULT_SET_MAX] = {
/*    sviLoadLIneEn, SviLoadLineVddC, TDC_VDDC_ThrottleReleaseLimitPerc, TDC_MAWt, TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac, BAPM_TEMP_GRADIENT */
	{1,               0xF,             0xFD,                               0x19,     5,               45,                  0,          0xB0000,
	{0x79,  0x253, 0x25D, 0xAE,  0x72,  0x80,    0x83,  0x86,  0x6F,  0xC8,    0xC9,  0xC9,  0x2F,  0x4D, 0x61},
	{0x17C, 0x172, 0x180, 0x1BC, 0x1B3, 0x1BD, 0x206, 0x200, 0x203, 0x25D, 0x25A, 0x255, 0x2C3, 0x2C5, 0x2B4 } },
};

void tonga_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *tonga_hwmgr = (struct tonga_hwmgr *)(hwmgr->backend);
	struct  phm_ppt_v1_information *table_info =
			(struct  phm_ppt_v1_information *)(hwmgr->pptable);
	uint32_t tmp = 0;

	if (table_info &&
			table_info->cac_dtp_table->usPowerTuneDataSetID <= POWERTUNE_DEFAULT_SET_MAX &&
			table_info->cac_dtp_table->usPowerTuneDataSetID)
		tonga_hwmgr->power_tune_defaults =
				&tonga_power_tune_data_set_array
				[table_info->cac_dtp_table->usPowerTuneDataSetID - 1];
	else
		tonga_hwmgr->power_tune_defaults = &tonga_power_tune_data_set_array[0];

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

	tonga_hwmgr->dte_tj_offset = tmp;

	if (!tmp) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_CAC);

		tonga_hwmgr->fast_watermark_threshold = 100;

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_PowerContainment)) {
			tmp = 1;
			tonga_hwmgr->enable_dte_feature = tmp ? false : true;
			tonga_hwmgr->enable_tdc_limit_feature = tmp ? true : false;
			tonga_hwmgr->enable_pkg_pwr_tracking_feature = tmp ? true : false;
		}
	}
}


int tonga_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct tonga_pt_defaults *defaults = data->power_tune_defaults;
	SMU72_Discrete_DpmTable  *dpm_table = &(data->smc_state_table);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	struct phm_cac_tdp_table *cac_dtp_table = table_info->cac_dtp_table;
	int  i, j, k;
	uint16_t *pdef1;
	uint16_t *pdef2;


	/* TDP number of fraction bits are changed from 8 to 7 for Fiji
	 * as requested by SMC team
	 */
	dpm_table->DefaultTdp = PP_HOST_TO_SMC_US(
			(uint16_t)(cac_dtp_table->usTDP * 256));
	dpm_table->TargetTdp = PP_HOST_TO_SMC_US(
			(uint16_t)(cac_dtp_table->usConfigurableTDP * 256));

	PP_ASSERT_WITH_CODE(cac_dtp_table->usTargetOperatingTemp <= 255,
			"Target Operating Temp is out of Range!",
			);

	dpm_table->GpuTjMax = (uint8_t)(cac_dtp_table->usTargetOperatingTemp);
	dpm_table->GpuTjHyst = 8;

	dpm_table->DTEAmbientTempBase = defaults->dte_ambient_temp_base;

	dpm_table->BAPM_TEMP_GRADIENT = PP_HOST_TO_SMC_UL(defaults->bamp_temp_gradient);
	pdef1 = defaults->bapmti_r;
	pdef2 = defaults->bapmti_rc;

	for (i = 0; i < SMU72_DTE_ITERATIONS; i++) {
		for (j = 0; j < SMU72_DTE_SOURCES; j++) {
			for (k = 0; k < SMU72_DTE_SINKS; k++) {
				dpm_table->BAPMTI_R[i][j][k] = PP_HOST_TO_SMC_US(*pdef1);
				dpm_table->BAPMTI_RC[i][j][k] = PP_HOST_TO_SMC_US(*pdef2);
				pdef1++;
				pdef2++;
			}
		}
	}

	return 0;
}

static int tonga_populate_svi_load_line(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	const struct tonga_pt_defaults *defaults = data->power_tune_defaults;

	data->power_tune_table.SviLoadLineEn = defaults->svi_load_line_en;
	data->power_tune_table.SviLoadLineVddC = defaults->svi_load_line_vddC;
	data->power_tune_table.SviLoadLineTrimVddC = 3;
	data->power_tune_table.SviLoadLineOffsetVddC = 0;

	return 0;
}

static int tonga_populate_tdc_limit(struct pp_hwmgr *hwmgr)
{
	uint16_t tdc_limit;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	const struct tonga_pt_defaults *defaults = data->power_tune_defaults;

	/* TDC number of fraction bits are changed from 8 to 7
	 * for Fiji as requested by SMC team
	 */
	tdc_limit = (uint16_t)(table_info->cac_dtp_table->usTDC * 256);
	data->power_tune_table.TDC_VDDC_PkgLimit =
			CONVERT_FROM_HOST_TO_SMC_US(tdc_limit);
	data->power_tune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
			defaults->tdc_vddc_throttle_release_limit_perc;
	data->power_tune_table.TDC_MAWt = defaults->tdc_mawt;

	return 0;
}

static int tonga_populate_dw8(struct pp_hwmgr *hwmgr, uint32_t fuse_table_offset)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	const struct tonga_pt_defaults *defaults = data->power_tune_defaults;
	uint32_t temp;

	if (tonga_read_smc_sram_dword(hwmgr->smumgr,
			fuse_table_offset +
			offsetof(SMU72_Discrete_PmFuses, TdcWaterfallCtl),
			(uint32_t *)&temp, data->sram_end))
		PP_ASSERT_WITH_CODE(false,
				"Attempt to read PmFuses.DW6 (SviLoadLineEn) from SMC Failed!",
				return -EINVAL);
	else
		data->power_tune_table.TdcWaterfallCtl = defaults->tdc_waterfall_ctl;

	return 0;
}

static int tonga_populate_temperature_scaler(struct pp_hwmgr *hwmgr)
{
	int i;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		data->power_tune_table.LPMLTemperatureScaler[i] = 0;

	return 0;
}

static int tonga_populate_fuzzy_fan(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	if ((hwmgr->thermal_controller.advanceFanControlParameters.
			usFanOutputSensitivity & (1 << 15)) ||
		(hwmgr->thermal_controller.advanceFanControlParameters.usFanOutputSensitivity == 0))
		hwmgr->thermal_controller.advanceFanControlParameters.
		usFanOutputSensitivity = hwmgr->thermal_controller.
			advanceFanControlParameters.usDefaultFanOutputSensitivity;

	data->power_tune_table.FuzzyFan_PwmSetDelta =
			PP_HOST_TO_SMC_US(hwmgr->thermal_controller.
					advanceFanControlParameters.usFanOutputSensitivity);
	return 0;
}

static int tonga_populate_gnb_lpml(struct pp_hwmgr *hwmgr)
{
	int i;
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 16; i++)
		data->power_tune_table.GnbLPML[i] = 0;

	return 0;
}

static int tonga_min_max_vgnb_lpml_id_from_bapm_vddc(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int tonga_populate_bapm_vddc_base_leakage_sidd(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v1_information *table_info =
			(struct phm_ppt_v1_information *)(hwmgr->pptable);
	uint16_t hi_sidd = data->power_tune_table.BapmVddCBaseLeakageHiSidd;
	uint16_t lo_sidd = data->power_tune_table.BapmVddCBaseLeakageLoSidd;
	struct phm_cac_tdp_table *cac_table = table_info->cac_dtp_table;

	hi_sidd = (uint16_t)(cac_table->usHighCACLeakage / 100 * 256);
	lo_sidd = (uint16_t)(cac_table->usLowCACLeakage / 100 * 256);

	data->power_tune_table.BapmVddCBaseLeakageHiSidd =
			CONVERT_FROM_HOST_TO_SMC_US(hi_sidd);
	data->power_tune_table.BapmVddCBaseLeakageLoSidd =
			CONVERT_FROM_HOST_TO_SMC_US(lo_sidd);

	return 0;
}

int tonga_populate_pm_fuses(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	uint32_t pm_fuse_table_offset;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (tonga_read_smc_sram_dword(hwmgr->smumgr,
				SMU72_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU72_Firmware_Header, PmFuseTable),
				&pm_fuse_table_offset, data->sram_end))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to get pm_fuse_table_offset Failed!",
					return -EINVAL);

		/* DW6 */
		if (tonga_populate_svi_load_line(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate SviLoadLine Failed!",
					return -EINVAL);
		/* DW7 */
		if (tonga_populate_tdc_limit(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TDCLimit Failed!", return -EINVAL);
		/* DW8 */
		if (tonga_populate_dw8(hwmgr, pm_fuse_table_offset))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TdcWaterfallCtl Failed !",
					return -EINVAL);

		/* DW9-DW12 */
		if (tonga_populate_temperature_scaler(hwmgr) != 0)
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate LPMLTemperatureScaler Failed!",
					return -EINVAL);

		/* DW13-DW14 */
		if (tonga_populate_fuzzy_fan(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate Fuzzy Fan Control parameters Failed!",
					return -EINVAL);

		/* DW15-DW18 */
		if (tonga_populate_gnb_lpml(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Failed!",
					return -EINVAL);

		/* DW19 */
		if (tonga_min_max_vgnb_lpml_id_from_bapm_vddc(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Min and Max Vid Failed!",
					return -EINVAL);

		/* DW20 */
		if (tonga_populate_bapm_vddc_base_leakage_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate BapmVddCBaseLeakage Hi and Lo Sidd Failed!",
					return -EINVAL);

		if (tonga_copy_bytes_to_smc(hwmgr->smumgr, pm_fuse_table_offset,
				(uint8_t *)&data->power_tune_table,
				sizeof(struct SMU72_Discrete_PmFuses), data->sram_end))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to download PmFuseTable Failed!",
					return -EINVAL);
	}
	return 0;
}

int tonga_enable_smc_cac(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_CAC)) {
		int smc_result;

		smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
				(uint16_t)(PPSMC_MSG_EnableCac));
		PP_ASSERT_WITH_CODE((smc_result == 0),
				"Failed to enable CAC in SMC.", result = -1);

		data->cac_enabled = (smc_result == 0) ? true : false;
	}
	return result;
}

int tonga_disable_smc_cac(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
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

int tonga_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);

	if (data->power_containment_features &
			POWERCONTAINMENT_FEATURE_PkgPwrLimit)
		return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_PkgPwrSetLimit, n);
	return 0;
}

static int tonga_set_overdriver_target_tdp(struct pp_hwmgr *pHwMgr, uint32_t target_tdp)
{
	return smum_send_msg_to_smc_with_parameter(pHwMgr->smumgr,
			PPSMC_MSG_OverDriveSetTargetTdp, target_tdp);
}

int tonga_enable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
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
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to enable DTE in SMC.", result = -1;);
			if (smc_result == 0)
				data->power_containment_features |= POWERCONTAINMENT_FEATURE_DTE;
		}

		if (data->enable_tdc_limit_feature) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_TDCLimitEnable));
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to enable TDCLimit in SMC.", result = -1;);
			if (smc_result == 0)
				data->power_containment_features |=
						POWERCONTAINMENT_FEATURE_TDCLimit;
		}

		if (data->enable_pkg_pwr_tracking_feature) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_PkgPwrLimitEnable));
			PP_ASSERT_WITH_CODE((smc_result == 0),
					"Failed to enable PkgPwrTracking in SMC.", result = -1;);
			if (smc_result == 0) {
				struct phm_cac_tdp_table *cac_table =
						table_info->cac_dtp_table;
				uint32_t default_limit =
					(uint32_t)(cac_table->usMaximumPowerDeliveryLimit * 256);

				data->power_containment_features |=
						POWERCONTAINMENT_FEATURE_PkgPwrLimit;

				if (tonga_set_power_limit(hwmgr, default_limit))
					printk(KERN_ERR "Failed to set Default Power Limit in SMC!");
			}
		}
	}
	return result;
}

int tonga_disable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct tonga_hwmgr *data = (struct tonga_hwmgr *)(hwmgr->backend);
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

int tonga_power_control_set_level(struct pp_hwmgr *hwmgr)
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
		result = tonga_set_overdriver_target_tdp(hwmgr, (uint32_t)target_tdp);
	}

	return result;
}
