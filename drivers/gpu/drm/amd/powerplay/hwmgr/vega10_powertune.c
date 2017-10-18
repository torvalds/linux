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

#include "hwmgr.h"
#include "vega10_hwmgr.h"
#include "vega10_powertune.h"
#include "vega10_smumgr.h"
#include "vega10_ppsmc.h"
#include "pp_debug.h"

void vega10_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data = (struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_tdp_table *tdp_table = table_info->tdp_table;
	PPTable_t *table = &(data->smc_state_table.pp_table);

	table->SocketPowerLimit = cpu_to_le16(
			tdp_table->usMaximumPowerDeliveryLimit);
	table->TdcLimit = cpu_to_le16(tdp_table->usTDC);
	table->EdcLimit = cpu_to_le16(tdp_table->usEDCLimit);
	table->TedgeLimit = cpu_to_le16(tdp_table->usTemperatureLimitTedge);
	table->ThotspotLimit = cpu_to_le16(tdp_table->usTemperatureLimitHotspot);
	table->ThbmLimit = cpu_to_le16(tdp_table->usTemperatureLimitHBM);
	table->Tvr_socLimit = cpu_to_le16(tdp_table->usTemperatureLimitVrVddc);
	table->Tvr_memLimit = cpu_to_le16(tdp_table->usTemperatureLimitVrMvdd);
	table->Tliquid1Limit = cpu_to_le16(tdp_table->usTemperatureLimitLiquid1);
	table->Tliquid2Limit = cpu_to_le16(tdp_table->usTemperatureLimitLiquid2);
	table->TplxLimit = cpu_to_le16(tdp_table->usTemperatureLimitPlx);
	table->LoadLineResistance =
			hwmgr->platform_descriptor.LoadLineSlope * 256;
	table->FitLimit = 0; /* Not used for Vega10 */

	table->Liquid1_I2C_address = tdp_table->ucLiquid1_I2C_address;
	table->Liquid2_I2C_address = tdp_table->ucLiquid2_I2C_address;
	table->Vr_I2C_address = tdp_table->ucVr_I2C_address;
	table->Plx_I2C_address = tdp_table->ucPlx_I2C_address;

	table->Liquid_I2C_LineSCL = tdp_table->ucLiquid_I2C_Line;
	table->Liquid_I2C_LineSDA = tdp_table->ucLiquid_I2C_LineSDA;

	table->Vr_I2C_LineSCL = tdp_table->ucVr_I2C_Line;
	table->Vr_I2C_LineSDA = tdp_table->ucVr_I2C_LineSDA;

	table->Plx_I2C_LineSCL = tdp_table->ucPlx_I2C_Line;
	table->Plx_I2C_LineSDA = tdp_table->ucPlx_I2C_LineSDA;
}

int vega10_set_power_limit(struct pp_hwmgr *hwmgr, uint32_t n)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	if (data->registry_data.enable_pkg_pwr_tracking_feature)
		return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
				PPSMC_MSG_SetPptLimit, n);

	return 0;
}

int vega10_enable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);
	struct phm_ppt_v2_information *table_info =
			(struct phm_ppt_v2_information *)(hwmgr->pptable);
	struct phm_tdp_table *tdp_table = table_info->tdp_table;
	uint32_t default_pwr_limit =
			(uint32_t)(tdp_table->usMaximumPowerDeliveryLimit);
	int result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (data->smu_features[GNLD_PPT].supported)
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
					true, data->smu_features[GNLD_PPT].smu_feature_bitmap),
					"Attempt to enable PPT feature Failed!",
					data->smu_features[GNLD_PPT].supported = false);

		if (data->smu_features[GNLD_TDC].supported)
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
					true, data->smu_features[GNLD_TDC].smu_feature_bitmap),
					"Attempt to enable PPT feature Failed!",
					data->smu_features[GNLD_TDC].supported = false);

		result = vega10_set_power_limit(hwmgr, default_pwr_limit);
		PP_ASSERT_WITH_CODE(!result,
				"Failed to set Default Power Limit in SMC!",
				return result);
	}

	return result;
}

int vega10_disable_power_containment(struct pp_hwmgr *hwmgr)
{
	struct vega10_hwmgr *data =
			(struct vega10_hwmgr *)(hwmgr->backend);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (data->smu_features[GNLD_PPT].supported)
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
					false, data->smu_features[GNLD_PPT].smu_feature_bitmap),
					"Attempt to disable PPT feature Failed!",
					data->smu_features[GNLD_PPT].supported = false);

		if (data->smu_features[GNLD_TDC].supported)
			PP_ASSERT_WITH_CODE(!vega10_enable_smc_features(hwmgr->smumgr,
					false, data->smu_features[GNLD_TDC].smu_feature_bitmap),
					"Attempt to disable PPT feature Failed!",
					data->smu_features[GNLD_TDC].supported = false);
	}

	return 0;
}

static int vega10_set_overdrive_target_percentage(struct pp_hwmgr *hwmgr,
		uint32_t adjust_percent)
{
	return smum_send_msg_to_smc_with_parameter(hwmgr->smumgr,
			PPSMC_MSG_OverDriveSetPercentage, adjust_percent);
}

int vega10_power_control_set_level(struct pp_hwmgr *hwmgr)
{
	int adjust_percent, result = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		adjust_percent =
				hwmgr->platform_descriptor.TDPAdjustmentPolarity ?
				hwmgr->platform_descriptor.TDPAdjustment :
				(-1 * hwmgr->platform_descriptor.TDPAdjustment);
		result = vega10_set_overdrive_target_percentage(hwmgr,
				(uint32_t)adjust_percent);
	}
	return result;
}
