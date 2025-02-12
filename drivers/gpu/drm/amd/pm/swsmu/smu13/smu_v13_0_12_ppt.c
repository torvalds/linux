/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#define SWSMU_CODE_LAYER_L2

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v13_0_12_pmfw.h"
#include "smu_v13_0_6_ppt.h"
#include "smu_v13_0_12_ppsmc.h"
#include "smu_v13_0.h"
#include "amdgpu_xgmi.h"
#include "amdgpu_fru_eeprom.h"
#include <linux/pci.h>
#include "smu_cmn.h"

#undef MP1_Public
#undef smnMP1_FIRMWARE_FLAGS

/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

#define SMU_13_0_12_FEA_MAP(smu_feature, smu_13_0_12_feature)                    \
	[smu_feature] = { 1, (smu_13_0_12_feature) }

#define FEATURE_MASK(feature) (1ULL << feature)
#define SMC_DPM_FEATURE                                                        \
	(FEATURE_MASK(FEATURE_DATA_CALCULATION) |                              \
	 FEATURE_MASK(FEATURE_DPM_GFXCLK) | FEATURE_MASK(FEATURE_DPM_FCLK))

const struct cmn2asic_mapping smu_v13_0_12_feature_mask_map[SMU_FEATURE_COUNT] = {
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DATA_CALCULATIONS_BIT, 		FEATURE_DATA_CALCULATION),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DPM_GFXCLK_BIT, 		FEATURE_DPM_GFXCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DPM_FCLK_BIT, 			FEATURE_DPM_FCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_GFXCLK_BIT, 			FEATURE_DS_GFXCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_SOCCLK_BIT, 			FEATURE_DS_SOCCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_LCLK_BIT, 			FEATURE_DS_LCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_FCLK_BIT, 			FEATURE_DS_FCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_PPT_BIT, 			FEATURE_PPT),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_TDC_BIT, 			FEATURE_TDC),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_APCC_DFLL_BIT, 			FEATURE_APCC_DFLL),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_MP1_CG_BIT, 			FEATURE_SMU_CG),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_FW_CTF_BIT, 			FEATURE_FW_CTF),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_THERMAL_BIT, 			FEATURE_THERMAL),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_SOC_PCC_BIT, 			FEATURE_SOC_PCC),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_XGMI_PER_LINK_PWR_DWN_BIT,	FEATURE_XGMI_PER_LINK_PWR_DOWN),
};

// clang-format off
const struct cmn2asic_msg_mapping smu_v13_0_12_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,			     PPSMC_MSG_TestMessage,			0),
	MSG_MAP(GetSmuVersion,			     PPSMC_MSG_GetSmuVersion,			1),
	MSG_MAP(GetDriverIfVersion,		     PPSMC_MSG_GetDriverIfVersion,		1),
	MSG_MAP(EnableAllSmuFeatures,		     PPSMC_MSG_EnableAllSmuFeatures,		0),
	MSG_MAP(DisableAllSmuFeatures,		     PPSMC_MSG_DisableAllSmuFeatures,		0),
	MSG_MAP(RequestI2cTransaction,		     PPSMC_MSG_RequestI2cTransaction,		0),
	MSG_MAP(GetMetricsTable,		     PPSMC_MSG_GetMetricsTable,			1),
	MSG_MAP(GetMetricsVersion,		     PPSMC_MSG_GetMetricsVersion,		1),
	MSG_MAP(GetEnabledSmuFeaturesHigh,	     PPSMC_MSG_GetEnabledSmuFeaturesHigh,	1),
	MSG_MAP(GetEnabledSmuFeaturesLow,	     PPSMC_MSG_GetEnabledSmuFeaturesLow,	1),
	MSG_MAP(SetDriverDramAddrHigh,		     PPSMC_MSG_SetDriverDramAddrHigh,		1),
	MSG_MAP(SetDriverDramAddrLow,		     PPSMC_MSG_SetDriverDramAddrLow,		1),
	MSG_MAP(SetToolsDramAddrHigh,		     PPSMC_MSG_SetToolsDramAddrHigh,		0),
	MSG_MAP(SetToolsDramAddrLow,		     PPSMC_MSG_SetToolsDramAddrLow,		0),
	MSG_MAP(SetSoftMinByFreq,		     PPSMC_MSG_SetSoftMinByFreq,		0),
	MSG_MAP(SetSoftMaxByFreq,		     PPSMC_MSG_SetSoftMaxByFreq,		1),
	MSG_MAP(GetMinDpmFreq,			     PPSMC_MSG_GetMinDpmFreq,			1),
	MSG_MAP(GetMaxDpmFreq,			     PPSMC_MSG_GetMaxDpmFreq,			1),
	MSG_MAP(GetDpmFreqByIndex,		     PPSMC_MSG_GetDpmFreqByIndex,		1),
	MSG_MAP(SetPptLimit,			     PPSMC_MSG_SetPptLimit,			0),
	MSG_MAP(GetPptLimit,			     PPSMC_MSG_GetPptLimit,			1),
	MSG_MAP(GfxDeviceDriverReset,		     PPSMC_MSG_GfxDriverReset,			SMU_MSG_RAS_PRI),
	MSG_MAP(DramLogSetDramAddrHigh,		     PPSMC_MSG_DramLogSetDramAddrHigh,		0),
	MSG_MAP(DramLogSetDramAddrLow,		     PPSMC_MSG_DramLogSetDramAddrLow,		0),
	MSG_MAP(DramLogSetDramSize,		     PPSMC_MSG_DramLogSetDramSize,		0),
	MSG_MAP(GetDebugData,			     PPSMC_MSG_GetDebugData,			0),
	MSG_MAP(SetNumBadHbmPagesRetired,	     PPSMC_MSG_SetNumBadHbmPagesRetired,	0),
	MSG_MAP(DFCstateControl,		     PPSMC_MSG_DFCstateControl,			0),
	MSG_MAP(GetGmiPwrDnHyst,		     PPSMC_MSG_GetGmiPwrDnHyst,			0),
	MSG_MAP(SetGmiPwrDnHyst,		     PPSMC_MSG_SetGmiPwrDnHyst,			0),
	MSG_MAP(GmiPwrDnControl,		     PPSMC_MSG_GmiPwrDnControl,			0),
	MSG_MAP(EnterGfxoff,			     PPSMC_MSG_EnterGfxoff,			0),
	MSG_MAP(ExitGfxoff,			     PPSMC_MSG_ExitGfxoff,			0),
	MSG_MAP(EnableDeterminism,		     PPSMC_MSG_EnableDeterminism,		0),
	MSG_MAP(DisableDeterminism,		     PPSMC_MSG_DisableDeterminism,		0),
	MSG_MAP(GfxDriverResetRecovery,		     PPSMC_MSG_GfxDriverResetRecovery,		0),
	MSG_MAP(GetMinGfxclkFrequency,               PPSMC_MSG_GetMinGfxDpmFreq,                1),
	MSG_MAP(GetMaxGfxclkFrequency,               PPSMC_MSG_GetMaxGfxDpmFreq,                1),
	MSG_MAP(SetSoftMinGfxclk,                    PPSMC_MSG_SetSoftMinGfxClk,                1),
	MSG_MAP(SetSoftMaxGfxClk,                    PPSMC_MSG_SetSoftMaxGfxClk,                1),
	MSG_MAP(PrepareMp1ForUnload,                 PPSMC_MSG_PrepareForDriverUnload,          0),
	MSG_MAP(GetCTFLimit,                         PPSMC_MSG_GetCTFLimit,                     0),
	MSG_MAP(GetThermalLimit,                     PPSMC_MSG_ReadThrottlerLimit,              0),
	MSG_MAP(ClearMcaOnRead,	                     PPSMC_MSG_ClearMcaOnRead,                  0),
	MSG_MAP(QueryValidMcaCount,                  PPSMC_MSG_QueryValidMcaCount,              SMU_MSG_RAS_PRI),
	MSG_MAP(QueryValidMcaCeCount,                PPSMC_MSG_QueryValidMcaCeCount,            SMU_MSG_RAS_PRI),
	MSG_MAP(McaBankDumpDW,                       PPSMC_MSG_McaBankDumpDW,                   SMU_MSG_RAS_PRI),
	MSG_MAP(McaBankCeDumpDW,                     PPSMC_MSG_McaBankCeDumpDW,                 SMU_MSG_RAS_PRI),
	MSG_MAP(SelectPLPDMode,                      PPSMC_MSG_SelectPLPDMode,                  0),
	MSG_MAP(RmaDueToBadPageThreshold,            PPSMC_MSG_RmaDueToBadPageThreshold,        0),
	MSG_MAP(SetThrottlingPolicy,                 PPSMC_MSG_SetThrottlingPolicy,             0),
	MSG_MAP(ResetSDMA,                           PPSMC_MSG_ResetSDMA,                       0),
	MSG_MAP(GetStaticMetricsTable,               PPSMC_MSG_GetStaticMetricsTable,           1),
};

static int smu_v13_0_12_get_enabled_mask(struct smu_context *smu,
					 uint64_t *feature_mask)
{
	int ret;

	ret = smu_cmn_get_enabled_mask(smu, feature_mask);

	if (ret == -EIO) {
		*feature_mask = 0;
		ret = 0;
	}

	return ret;
}

static int smu_v13_0_12_fru_get_product_info(struct smu_context *smu,
					     StaticMetricsTable_t *static_metrics)
{
	struct amdgpu_fru_info *fru_info;
	struct amdgpu_device *adev = smu->adev;

	if (!adev->fru_info) {
		adev->fru_info = kzalloc(sizeof(*adev->fru_info), GFP_KERNEL);
		if (!adev->fru_info)
			return -ENOMEM;
	}

	fru_info = adev->fru_info;
	strscpy(fru_info->product_number, static_metrics->ProductInfo.ModelNumber,
		sizeof(fru_info->product_number));
	strscpy(fru_info->product_name, static_metrics->ProductInfo.Name,
		sizeof(fru_info->product_name));
	strscpy(fru_info->serial, static_metrics->ProductInfo.Serial,
		sizeof(fru_info->serial));
	strscpy(fru_info->manufacturer_name, static_metrics->ProductInfo.ManufacturerName,
		sizeof(fru_info->manufacturer_name));
	strscpy(fru_info->fru_id, static_metrics->ProductInfo.FruId,
		sizeof(fru_info->fru_id));

	return 0;
}

int smu_v13_0_12_get_max_metrics_size(void)
{
	return sizeof(StaticMetricsTable_t);
}

static int smu_v13_0_12_get_static_metrics_table(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	uint32_t table_size = smu_table->tables[SMU_TABLE_SMU_METRICS].size;
	struct smu_table *table = &smu_table->driver_table;
	int ret;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetStaticMetricsTable, NULL);
	if (ret) {
		dev_info(smu->adev->dev,
			 "Failed to export static metrics table!\n");
		return ret;
	}

	amdgpu_asic_invalidate_hdp(smu->adev, NULL);
	memcpy(smu_table->metrics_table, table->cpu_addr, table_size);

	return 0;
}

int smu_v13_0_12_setup_driver_pptable(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	StaticMetricsTable_t *static_metrics = (StaticMetricsTable_t *)smu_table->metrics_table;
	struct PPTable_t *pptable =
		(struct PPTable_t *)smu_table->driver_pptable;
	int ret, i;

	if (!pptable->Init) {
		ret = smu_v13_0_12_get_static_metrics_table(smu);
		if (ret)
			return ret;

		pptable->MaxSocketPowerLimit =
			SMUQ10_ROUND(static_metrics->MaxSocketPowerLimit);
		pptable->MaxGfxclkFrequency =
			SMUQ10_ROUND(static_metrics->MaxGfxclkFrequency);
		pptable->MinGfxclkFrequency =
			SMUQ10_ROUND(static_metrics->MinGfxclkFrequency);

		for (i = 0; i < 4; ++i) {
			pptable->FclkFrequencyTable[i] =
				SMUQ10_ROUND(static_metrics->FclkFrequencyTable[i]);
			pptable->UclkFrequencyTable[i] =
				SMUQ10_ROUND(static_metrics->UclkFrequencyTable[i]);
			pptable->SocclkFrequencyTable[i] =
				SMUQ10_ROUND(static_metrics->SocclkFrequencyTable[i]);
			pptable->VclkFrequencyTable[i] =
				SMUQ10_ROUND(static_metrics->VclkFrequencyTable[i]);
			pptable->DclkFrequencyTable[i] =
				SMUQ10_ROUND(static_metrics->DclkFrequencyTable[i]);
			pptable->LclkFrequencyTable[i] =
				SMUQ10_ROUND(static_metrics->LclkFrequencyTable[i]);
		}

		/* use AID0 serial number by default */
		pptable->PublicSerialNumber_AID =
			static_metrics->PublicSerialNumber_AID[0];
		ret = smu_v13_0_12_fru_get_product_info(smu, static_metrics);
		if (ret)
			return ret;

		pptable->Init = true;
	}

	return 0;
}

bool smu_v13_0_12_is_dpm_running(struct smu_context *smu)
{
	int ret;
	uint64_t feature_enabled;

	ret = smu_v13_0_12_get_enabled_mask(smu, &feature_enabled);

	if (ret)
		return false;

	return !!(feature_enabled & SMC_DPM_FEATURE);
}
