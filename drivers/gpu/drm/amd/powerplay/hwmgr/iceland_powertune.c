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
 * Author: Huang Rui <ray.huang@amd.com>
 *
 */

#include "amdgpu.h"
#include "hwmgr.h"
#include "smumgr.h"
#include "iceland_hwmgr.h"
#include "iceland_powertune.h"
#include "iceland_smumgr.h"
#include "smu71_discrete.h"
#include "smu71.h"
#include "pp_debug.h"
#include "cgs_common.h"
#include "pp_endian.h"

#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"

#define VOLTAGE_SCALE  4
#define POWERTUNE_DEFAULT_SET_MAX    1

#define DEVICE_ID_VI_ICELAND_M_6900	0x6900
#define DEVICE_ID_VI_ICELAND_M_6901	0x6901
#define DEVICE_ID_VI_ICELAND_M_6902	0x6902
#define DEVICE_ID_VI_ICELAND_M_6903	0x6903


struct iceland_pt_defaults defaults_iceland =
{
	/*
	 * sviLoadLIneEn, SviLoadLineVddC, TDC_VDDC_ThrottleReleaseLimitPerc,
	 * TDC_MAWt, TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac, BAPM_TEMP_GRADIENT
	 */
	1, 0xF, 0xFD, 0x19, 5, 45, 0, 0xB0000,
	{ 0x79,  0x253, 0x25D, 0xAE,  0x72,  0x80,  0x83,  0x86,  0x6F,  0xC8,  0xC9,  0xC9,  0x2F,  0x4D,  0x61  },
	{ 0x17C, 0x172, 0x180, 0x1BC, 0x1B3, 0x1BD, 0x206, 0x200, 0x203, 0x25D, 0x25A, 0x255, 0x2C3, 0x2C5, 0x2B4 }
};

/* 35W - XT, XTL */
struct iceland_pt_defaults defaults_icelandxt =
{
	/*
	 * sviLoadLIneEn, SviLoadLineVddC,
	 * TDC_VDDC_ThrottleReleaseLimitPerc, TDC_MAWt,
	 * TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac,
	 * BAPM_TEMP_GRADIENT
	 */
	1, 0xF, 0xFD, 0x19, 5, 45, 0, 0x0,
	{ 0xA7,  0x0, 0x0, 0xB5,  0x0, 0x0, 0x9F,  0x0, 0x0, 0xD6,  0x0, 0x0, 0xD7,  0x0, 0x0},
	{ 0x1EA, 0x0, 0x0, 0x224, 0x0, 0x0, 0x25E, 0x0, 0x0, 0x28E, 0x0, 0x0, 0x2AB, 0x0, 0x0}
};

/* 25W - PRO, LE */
struct iceland_pt_defaults defaults_icelandpro =
{
	/*
	 * sviLoadLIneEn, SviLoadLineVddC,
	 * TDC_VDDC_ThrottleReleaseLimitPerc, TDC_MAWt,
	 * TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac,
	 * BAPM_TEMP_GRADIENT
	 */
	1, 0xF, 0xFD, 0x19, 5, 45, 0, 0x0,
	{ 0xB7,  0x0, 0x0, 0xC3,  0x0, 0x0, 0xB5,  0x0, 0x0, 0xEA,  0x0, 0x0, 0xE6,  0x0, 0x0},
	{ 0x1EA, 0x0, 0x0, 0x224, 0x0, 0x0, 0x25E, 0x0, 0x0, 0x28E, 0x0, 0x0, 0x2AB, 0x0, 0x0}
};

void iceland_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint32_t tmp = 0;
	struct cgs_system_info sys_info = {0};
	uint32_t pdev_id;

	sys_info.size = sizeof(struct cgs_system_info);
	sys_info.info_id = CGS_SYSTEM_INFO_PCIE_DEV;
	cgs_query_system_info(hwmgr->device, &sys_info);
	pdev_id = (uint32_t)sys_info.value;

	switch (pdev_id) {
	case DEVICE_ID_VI_ICELAND_M_6900:
	case DEVICE_ID_VI_ICELAND_M_6903:
		data->power_tune_defaults = &defaults_icelandxt;
		break;

	case DEVICE_ID_VI_ICELAND_M_6901:
	case DEVICE_ID_VI_ICELAND_M_6902:
		data->power_tune_defaults = &defaults_icelandpro;
		break;
	default:
	    /* TODO: need to assign valid defaults */
	    data->power_tune_defaults = &defaults_iceland;
	    pr_warning("Unknown V.I. Device ID.\n");
	    break;
	}

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

	data->ul_dte_tj_offset = tmp;

	if (!tmp) {
		phm_cap_set(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_CAC);

		data->fast_watermark_threshold = 100;

		if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
					PHM_PlatformCaps_PowerContainment)) {
			tmp = 1;
			data->enable_dte_feature = tmp ? false : true;
			data->enable_tdc_limit_feature = tmp ? true : false;
			data->enable_pkg_pwr_tracking_feature = tmp ? true : false;
		}
	}
}

int iceland_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	struct iceland_pt_defaults *defaults = data->power_tune_defaults;
	SMU71_Discrete_DpmTable  *dpm_table = &(data->smc_state_table);
	struct phm_cac_tdp_table *cac_dtp_table = hwmgr->dyn_state.cac_dtp_table;
	struct phm_ppm_table *ppm = hwmgr->dyn_state.ppm_parameter_table;
	uint16_t *def1, *def2;
	int i, j, k;

	/*
	 * TDP number of fraction bits are changed from 8 to 7 for Iceland
	 * as requested by SMC team
	 */
	dpm_table->DefaultTdp = PP_HOST_TO_SMC_US((uint16_t)(cac_dtp_table->usTDP * 256));
	dpm_table->TargetTdp = PP_HOST_TO_SMC_US((uint16_t)(cac_dtp_table->usConfigurableTDP * 256));

	dpm_table->DTETjOffset = (uint8_t)data->ul_dte_tj_offset;

	dpm_table->GpuTjMax = (uint8_t)(data->thermal_temp_setting.temperature_high / PP_TEMPERATURE_UNITS_PER_CENTIGRADES);
	dpm_table->GpuTjHyst = 8;

	dpm_table->DTEAmbientTempBase = defaults->dte_ambient_temp_base;

	/* The following are for new Iceland Multi-input fan/thermal control */
	if(NULL != ppm) {
		dpm_table->PPM_PkgPwrLimit = (uint16_t)ppm->dgpu_tdp * 256 / 1000;
		dpm_table->PPM_TemperatureLimit = (uint16_t)ppm->tj_max * 256;
	} else {
		dpm_table->PPM_PkgPwrLimit = 0;
		dpm_table->PPM_TemperatureLimit = 0;
	}

	CONVERT_FROM_HOST_TO_SMC_US(dpm_table->PPM_PkgPwrLimit);
	CONVERT_FROM_HOST_TO_SMC_US(dpm_table->PPM_TemperatureLimit);

	dpm_table->BAPM_TEMP_GRADIENT = PP_HOST_TO_SMC_UL(defaults->bamp_temp_gradient);
	def1 = defaults->bapmti_r;
	def2 = defaults->bapmti_rc;

	for (i = 0; i < SMU71_DTE_ITERATIONS; i++) {
		for (j = 0; j < SMU71_DTE_SOURCES; j++) {
			for (k = 0; k < SMU71_DTE_SINKS; k++) {
				dpm_table->BAPMTI_R[i][j][k] = PP_HOST_TO_SMC_US(*def1);
				dpm_table->BAPMTI_RC[i][j][k] = PP_HOST_TO_SMC_US(*def2);
				def1++;
				def2++;
			}
		}
	}

	return 0;
}

static int iceland_populate_svi_load_line(struct pp_hwmgr *hwmgr)
{
    struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
    const struct iceland_pt_defaults *defaults = data->power_tune_defaults;

    data->power_tune_table.SviLoadLineEn = defaults->svi_load_line_en;
    data->power_tune_table.SviLoadLineVddC = defaults->svi_load_line_vddc;
    data->power_tune_table.SviLoadLineTrimVddC = 3;
    data->power_tune_table.SviLoadLineOffsetVddC = 0;

    return 0;
}

static int iceland_populate_tdc_limit(struct pp_hwmgr *hwmgr)
{
	uint16_t tdc_limit;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	const struct iceland_pt_defaults *defaults = data->power_tune_defaults;

	/* TDC number of fraction bits are changed from 8 to 7
	 * for Iceland as requested by SMC team
	 */
	tdc_limit = (uint16_t)(hwmgr->dyn_state.cac_dtp_table->usTDC * 256);
	data->power_tune_table.TDC_VDDC_PkgLimit =
			CONVERT_FROM_HOST_TO_SMC_US(tdc_limit);
	data->power_tune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
			defaults->tdc_vddc_throttle_release_limit_perc;
	data->power_tune_table.TDC_MAWt = defaults->tdc_mawt;

	return 0;
}

static int iceland_populate_dw8(struct pp_hwmgr *hwmgr, uint32_t fuse_table_offset)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	const struct iceland_pt_defaults *defaults = data->power_tune_defaults;
	uint32_t temp;

	if (iceland_read_smc_sram_dword(hwmgr->smumgr,
			fuse_table_offset +
			offsetof(SMU71_Discrete_PmFuses, TdcWaterfallCtl),
			(uint32_t *)&temp, data->sram_end))
		PP_ASSERT_WITH_CODE(false,
				"Attempt to read PmFuses.DW6 (SviLoadLineEn) from SMC Failed!",
				return -EINVAL);
	else
		data->power_tune_table.TdcWaterfallCtl = defaults->tdc_waterfall_ctl;

	return 0;
}

static int iceland_populate_temperature_scaler(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int iceland_populate_gnb_lpml(struct pp_hwmgr *hwmgr)
{
	int i;
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 8; i++)
		data->power_tune_table.GnbLPML[i] = 0;

	return 0;
}

static int iceland_min_max_vgnb_lpml_id_from_bapm_vddc(struct pp_hwmgr *hwmgr)
{
    return 0;
}

static int iceland_populate_bapm_vddc_base_leakage_sidd(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint16_t HiSidd = data->power_tune_table.BapmVddCBaseLeakageHiSidd;
	uint16_t LoSidd = data->power_tune_table.BapmVddCBaseLeakageLoSidd;
	struct phm_cac_tdp_table *cac_table = hwmgr->dyn_state.cac_dtp_table;

	HiSidd = (uint16_t)(cac_table->usHighCACLeakage / 100 * 256);
	LoSidd = (uint16_t)(cac_table->usLowCACLeakage / 100 * 256);

	data->power_tune_table.BapmVddCBaseLeakageHiSidd =
			CONVERT_FROM_HOST_TO_SMC_US(HiSidd);
	data->power_tune_table.BapmVddCBaseLeakageLoSidd =
			CONVERT_FROM_HOST_TO_SMC_US(LoSidd);

	return 0;
}

int iceland_populate_pm_fuses(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	uint32_t pm_fuse_table_offset;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (iceland_read_smc_sram_dword(hwmgr->smumgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, PmFuseTable),
				&pm_fuse_table_offset, data->sram_end))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to get pm_fuse_table_offset Failed!",
					return -EINVAL);

		/* DW0 - DW3 */
		if (iceland_populate_bapm_vddc_vid_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate bapm vddc vid Failed!",
					return -EINVAL);

		/* DW4 - DW5 */
		if (iceland_populate_vddc_vid(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate vddc vid Failed!",
					return -EINVAL);

		/* DW6 */
		if (iceland_populate_svi_load_line(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate SviLoadLine Failed!",
					return -EINVAL);
		/* DW7 */
		if (iceland_populate_tdc_limit(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TDCLimit Failed!", return -EINVAL);
		/* DW8 */
		if (iceland_populate_dw8(hwmgr, pm_fuse_table_offset))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TdcWaterfallCtl, "
					"LPMLTemperature Min and Max Failed!",
					return -EINVAL);

		/* DW9-DW12 */
		if (0 != iceland_populate_temperature_scaler(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate LPMLTemperatureScaler Failed!",
					return -EINVAL);

		/* DW13-DW16 */
		if (iceland_populate_gnb_lpml(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Failed!",
					return -EINVAL);

		/* DW17 */
		if (iceland_min_max_vgnb_lpml_id_from_bapm_vddc(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Min and Max Vid Failed!",
					return -EINVAL);

		/* DW18 */
		if (iceland_populate_bapm_vddc_base_leakage_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate BapmVddCBaseLeakage Hi and Lo Sidd Failed!",
					return -EINVAL);

		if (iceland_copy_bytes_to_smc(hwmgr->smumgr, pm_fuse_table_offset,
				(uint8_t *)&data->power_tune_table,
				sizeof(struct SMU71_Discrete_PmFuses), data->sram_end))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to download PmFuseTable Failed!",
					return -EINVAL);
	}
	return 0;
}

int iceland_enable_smc_cac(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
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

static int iceland_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);

	if(data->power_containment_features &
			POWERCONTAINMENT_FEATURE_PkgPwrLimit)
		return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_PkgPwrSetLimit, n);
	return 0;
}

static int iceland_set_overdriver_target_tdp(struct pp_hwmgr *pHwMgr, uint32_t target_tdp)
{
	return smum_send_msg_to_smc_with_parameter(pHwMgr->smumgr,
			PPSMC_MSG_OverDriveSetTargetTdp, target_tdp);
}

int iceland_enable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct iceland_hwmgr *data = (struct iceland_hwmgr *)(hwmgr->backend);
	SMU71_Discrete_DpmTable *dpm_table = &data->smc_state_table;
	int smc_result;
	int result = 0;
	uint32_t is_asic_kicker;

	data->power_containment_features = 0;
	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		is_asic_kicker = cgs_read_register(hwmgr->device, mmCC_BIF_BX_STRAP2);
		is_asic_kicker = (is_asic_kicker >> 12) & 0x01;

		if (data->enable_bapm_feature &&
			(!is_asic_kicker ||
			 phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				 PHM_PlatformCaps_DisableUsingActualTemperatureForPowerCalc))) {
			smc_result = smum_send_msg_to_smc(hwmgr->smumgr,
					(uint16_t)(PPSMC_MSG_EnableDTE));
			PP_ASSERT_WITH_CODE((0 == smc_result),
					"Failed to enable BAPM in SMC.", result = -1;);
			if (0 == smc_result)
				data->power_containment_features |= POWERCONTAINMENT_FEATURE_BAPM;
		}

		if (is_asic_kicker && !phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
				PHM_PlatformCaps_DisableUsingActualTemperatureForPowerCalc))
			dpm_table->DTEMode = 2;

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
						hwmgr->dyn_state.cac_dtp_table;
				uint32_t default_limit =
					(uint32_t)(cac_table->usMaximumPowerDeliveryLimit * 256);

				data->power_containment_features |=
						POWERCONTAINMENT_FEATURE_PkgPwrLimit;

				if (iceland_set_power_limit(hwmgr, default_limit))
					printk(KERN_ERR "Failed to set Default Power Limit in SMC!");
			}
		}
	}
	return result;
}

int iceland_power_control_set_level(struct pp_hwmgr *hwmgr)
{
	struct phm_cac_tdp_table *cac_table = hwmgr->dyn_state.cac_dtp_table;
	int adjust_percent, target_tdp;
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		/* adjustment percentage has already been validated */
		adjust_percent = hwmgr->platform_descriptor.TDPAdjustmentPolarity ?
				hwmgr->platform_descriptor.TDPAdjustment :
				(-1 * hwmgr->platform_descriptor.TDPAdjustment);
		/*
		 * SMC requested that target_tdp to be 7 bit fraction in DPM table
		 * but message to be 8 bit fraction for messages
		 */
		target_tdp = ((100 + adjust_percent) * (int)(cac_table->usTDP * 256)) / 100;
		result = iceland_set_overdriver_target_tdp(hwmgr, (uint32_t)target_tdp);
	}

	return result;
}
