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

#define NUM_JPEG_RINGS_FW	10
#define NUM_JPEG_RINGS_GPU_METRICS(gpu_metrics) \
	(ARRAY_SIZE(gpu_metrics->xcp_stats[0].jpeg_busy) / 4)

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
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_VCN_BIT,			FEATURE_DS_VCN),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_MP1CLK_BIT,			FEATURE_DS_MP1CLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_MPIOCLK_BIT,			FEATURE_DS_MPIOCLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_DS_MP0CLK_BIT,			FEATURE_DS_MP0CLK),
	SMU_13_0_12_FEA_MAP(SMU_FEATURE_PIT_BIT,			FEATURE_PIT),
};

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
	MSG_MAP(GfxDeviceDriverReset,		     PPSMC_MSG_GfxDriverReset,			SMU_MSG_RAS_PRI | SMU_MSG_NO_PRECHECK),
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
	MSG_MAP(ResetVCN,                            PPSMC_MSG_ResetVCN,                        0),
	MSG_MAP(GetStaticMetricsTable,               PPSMC_MSG_GetStaticMetricsTable,           1),
	MSG_MAP(GetSystemMetricsTable,               PPSMC_MSG_GetSystemMetricsTable,           1),
};

int smu_v13_0_12_tables_init(struct smu_context *smu)
{
	struct amdgpu_baseboard_temp_metrics_v1_0 *baseboard_temp_metrics;
	struct amdgpu_gpuboard_temp_metrics_v1_0 *gpuboard_temp_metrics;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	struct smu_table_cache *cache;
	int ret;

	ret = smu_table_cache_init(smu, SMU_TABLE_PMFW_SYSTEM_METRICS,
				   smu_v13_0_12_get_system_metrics_size(), 5);

	if (ret)
		return ret;

	ret = smu_table_cache_init(smu, SMU_TABLE_BASEBOARD_TEMP_METRICS,
				   sizeof(*baseboard_temp_metrics), 50);
	if (ret)
		return ret;
	/* Initialize base board temperature metrics */
	cache = &(tables[SMU_TABLE_BASEBOARD_TEMP_METRICS].cache);
	baseboard_temp_metrics =
		(struct amdgpu_baseboard_temp_metrics_v1_0 *) cache->buffer;
	smu_cmn_init_baseboard_temp_metrics(baseboard_temp_metrics, 1, 0);
	/* Initialize GPU board temperature metrics */
	ret = smu_table_cache_init(smu, SMU_TABLE_GPUBOARD_TEMP_METRICS,
				   sizeof(*gpuboard_temp_metrics), 50);
	if (ret) {
		smu_table_cache_fini(smu, SMU_TABLE_PMFW_SYSTEM_METRICS);
		smu_table_cache_fini(smu, SMU_TABLE_BASEBOARD_TEMP_METRICS);
		return ret;
	}
	cache = &(tables[SMU_TABLE_GPUBOARD_TEMP_METRICS].cache);
	gpuboard_temp_metrics = (struct amdgpu_gpuboard_temp_metrics_v1_0 *)cache->buffer;
	smu_cmn_init_gpuboard_temp_metrics(gpuboard_temp_metrics, 1, 0);

	return 0;
}

void smu_v13_0_12_tables_fini(struct smu_context *smu)
{
	smu_table_cache_fini(smu, SMU_TABLE_BASEBOARD_TEMP_METRICS);
	smu_table_cache_fini(smu, SMU_TABLE_GPUBOARD_TEMP_METRICS);
	smu_table_cache_fini(smu, SMU_TABLE_PMFW_SYSTEM_METRICS);
}

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
	return max(sizeof(StaticMetricsTable_t), sizeof(MetricsTable_t));
}

size_t smu_v13_0_12_get_system_metrics_size(void)
{
	return sizeof(SystemMetricsTable_t);
}

static void smu_v13_0_12_init_xgmi_data(struct smu_context *smu,
					StaticMetricsTable_t *static_metrics)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	uint16_t max_speed;
	uint8_t max_width;
	int ret;

	if (smu_table->tables[SMU_TABLE_SMU_METRICS].version >= 0x13) {
		max_width = (uint8_t)static_metrics->MaxXgmiWidth;
		max_speed = (uint16_t)static_metrics->MaxXgmiBitrate;
		ret = 0;
	} else {
		MetricsTable_t *metrics = (MetricsTable_t *)smu_table->metrics_table;

		ret = smu_v13_0_6_get_metrics_table(smu, NULL, true);
		if (!ret) {
			max_width = (uint8_t)metrics->XgmiWidth;
			max_speed = (uint16_t)metrics->XgmiBitrate;
		}
	}
	if (!ret)
		amgpu_xgmi_set_max_speed_width(smu->adev, max_speed, max_width);
}

int smu_v13_0_12_setup_driver_pptable(struct smu_context *smu)
{
	struct smu_13_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_table_context *smu_table = &smu->smu_table;
	StaticMetricsTable_t *static_metrics = (StaticMetricsTable_t *)smu_table->metrics_table;
	struct PPTable_t *pptable =
		(struct PPTable_t *)smu_table->driver_pptable;
	uint32_t table_version;
	int ret, i, n;

	if (!pptable->Init) {
		ret = smu_v13_0_6_get_static_metrics_table(smu);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetMetricsVersion,
					   &table_version);
		if (ret)
			return ret;
		smu_table->tables[SMU_TABLE_SMU_METRICS].version =
			table_version;

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

		amdgpu_device_set_uid(smu->adev->uid_info, AMDGPU_UID_TYPE_SOC,
				      0, pptable->PublicSerialNumber_AID);
		n = ARRAY_SIZE(static_metrics->PublicSerialNumber_AID);
		for (i = 0; i < n; i++) {
			amdgpu_device_set_uid(
				smu->adev->uid_info, AMDGPU_UID_TYPE_AID, i,
				static_metrics->PublicSerialNumber_AID[i]);
		}
		n = ARRAY_SIZE(static_metrics->PublicSerialNumber_XCD);
		for (i = 0; i < n; i++) {
			amdgpu_device_set_uid(
				smu->adev->uid_info, AMDGPU_UID_TYPE_XCD, i,
				static_metrics->PublicSerialNumber_XCD[i]);
		}

		ret = smu_v13_0_12_fru_get_product_info(smu, static_metrics);
		if (ret)
			return ret;

		if (smu_v13_0_6_cap_supported(smu, SMU_CAP(BOARD_VOLTAGE))) {
			if (!static_metrics->InputTelemetryVoltageInmV) {
				dev_warn(smu->adev->dev, "Invalid board voltage %d\n",
						static_metrics->InputTelemetryVoltageInmV);
			}
			dpm_context->board_volt = static_metrics->InputTelemetryVoltageInmV;
		}
		if (smu_v13_0_6_cap_supported(smu, SMU_CAP(PLDM_VERSION)) &&
			static_metrics->pldmVersion[0] != 0xFFFFFFFF)
			smu->adev->firmware.pldm_version =
				static_metrics->pldmVersion[0];
		if (smu_v13_0_6_cap_supported(smu, SMU_CAP(NPM_METRICS)))
			pptable->MaxNodePowerLimit =
				SMUQ10_ROUND(static_metrics->MaxNodePowerLimit);
		smu_v13_0_12_init_xgmi_data(smu, static_metrics);
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

int smu_v13_0_12_get_smu_metrics_data(struct smu_context *smu,
				      MetricsMember_t member,
				      uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	MetricsTable_t *metrics = (MetricsTable_t *)smu_table->metrics_table;
	struct amdgpu_device *adev = smu->adev;
	int xcc_id;

	/* For clocks with multiple instances, only report the first one */
	switch (member) {
	case METRICS_CURR_GFXCLK:
	case METRICS_AVERAGE_GFXCLK:
		xcc_id = GET_INST(GC, 0);
		*value = SMUQ10_ROUND(metrics->GfxclkFrequency[xcc_id]);
		break;
	case METRICS_CURR_SOCCLK:
	case METRICS_AVERAGE_SOCCLK:
		*value = SMUQ10_ROUND(metrics->SocclkFrequency[0]);
		break;
	case METRICS_CURR_UCLK:
	case METRICS_AVERAGE_UCLK:
		*value = SMUQ10_ROUND(metrics->UclkFrequency);
		break;
	case METRICS_CURR_VCLK:
		*value = SMUQ10_ROUND(metrics->VclkFrequency[0]);
		break;
	case METRICS_CURR_DCLK:
		*value = SMUQ10_ROUND(metrics->DclkFrequency[0]);
		break;
	case METRICS_CURR_FCLK:
		*value = SMUQ10_ROUND(metrics->FclkFrequency);
		break;
	case METRICS_AVERAGE_GFXACTIVITY:
		*value = SMUQ10_ROUND(metrics->SocketGfxBusy);
		break;
	case METRICS_AVERAGE_MEMACTIVITY:
		*value = SMUQ10_ROUND(metrics->DramBandwidthUtilization);
		break;
	case METRICS_CURR_SOCKETPOWER:
		*value = SMUQ10_ROUND(metrics->SocketPower) << 8;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = SMUQ10_ROUND(metrics->MaxSocketTemperature) *
			 SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_MEM:
		*value = SMUQ10_ROUND(metrics->MaxHbmTemperature) *
			 SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	/* This is the max of all VRs and not just SOC VR.
	 * No need to define another data type for the same.
	 */
	case METRICS_TEMPERATURE_VRSOC:
		*value = SMUQ10_ROUND(metrics->MaxVrTemperature) *
			 SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	return 0;
}

static int smu_v13_0_12_get_system_metrics_table(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *table = &smu_table->driver_table;
	struct smu_table *tables = smu_table->tables;
	struct smu_table *sys_table;
	int ret;

	sys_table = &tables[SMU_TABLE_PMFW_SYSTEM_METRICS];
	if (smu_table_cache_is_valid(sys_table))
		return 0;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetSystemMetricsTable, NULL);
	if (ret) {
		dev_info(smu->adev->dev,
			 "Failed to export system metrics table!\n");
		return ret;
	}

	amdgpu_asic_invalidate_hdp(smu->adev, NULL);
	smu_table_cache_update_time(sys_table, jiffies);
	memcpy(sys_table->cache.buffer, table->cpu_addr,
	       smu_v13_0_12_get_system_metrics_size());

	return 0;
}

static enum amdgpu_node_temp smu_v13_0_12_get_node_sensor_type(NODE_TEMP_e type)
{
	switch (type) {
	case NODE_TEMP_RETIMER:
		return AMDGPU_RETIMER_X_TEMP;
	case NODE_TEMP_IBC_TEMP:
		return AMDGPU_OAM_X_IBC_TEMP;
	case NODE_TEMP_IBC_2_TEMP:
		return AMDGPU_OAM_X_IBC_2_TEMP;
	case NODE_TEMP_VDD18_VR_TEMP:
		return AMDGPU_OAM_X_VDD18_VR_TEMP;
	case NODE_TEMP_04_HBM_B_VR_TEMP:
		return AMDGPU_OAM_X_04_HBM_B_VR_TEMP;
	case NODE_TEMP_04_HBM_D_VR_TEMP:
		return AMDGPU_OAM_X_04_HBM_D_VR_TEMP;
	default:
		return -EINVAL;
	}
}

static enum amdgpu_vr_temp smu_v13_0_12_get_vr_sensor_type(SVI_TEMP_e type)
{
	switch (type) {
	case SVI_VDDCR_VDD0_TEMP:
		return AMDGPU_VDDCR_VDD0_TEMP;
	case SVI_VDDCR_VDD1_TEMP:
		return AMDGPU_VDDCR_VDD1_TEMP;
	case SVI_VDDCR_VDD2_TEMP:
		return AMDGPU_VDDCR_VDD2_TEMP;
	case SVI_VDDCR_VDD3_TEMP:
		return AMDGPU_VDDCR_VDD3_TEMP;
	case SVI_VDDCR_SOC_A_TEMP:
		return AMDGPU_VDDCR_SOC_A_TEMP;
	case SVI_VDDCR_SOC_C_TEMP:
		return AMDGPU_VDDCR_SOC_C_TEMP;
	case SVI_VDDCR_SOCIO_A_TEMP:
		return AMDGPU_VDDCR_SOCIO_A_TEMP;
	case SVI_VDDCR_SOCIO_C_TEMP:
		return AMDGPU_VDDCR_SOCIO_C_TEMP;
	case SVI_VDD_085_HBM_TEMP:
		return AMDGPU_VDD_085_HBM_TEMP;
	case SVI_VDDCR_11_HBM_B_TEMP:
		return AMDGPU_VDDCR_11_HBM_B_TEMP;
	case SVI_VDDCR_11_HBM_D_TEMP:
		return AMDGPU_VDDCR_11_HBM_D_TEMP;
	case SVI_VDD_USR_TEMP:
		return AMDGPU_VDD_USR_TEMP;
	case SVI_VDDIO_11_E32_TEMP:
		return AMDGPU_VDDIO_11_E32_TEMP;
	default:
		return -EINVAL;
	}
}

static enum amdgpu_system_temp smu_v13_0_12_get_system_sensor_type(SYSTEM_TEMP_e type)
{
	switch (type) {
	case SYSTEM_TEMP_UBB_FPGA:
		return AMDGPU_UBB_FPGA_TEMP;
	case SYSTEM_TEMP_UBB_FRONT:
		return AMDGPU_UBB_FRONT_TEMP;
	case SYSTEM_TEMP_UBB_BACK:
		return AMDGPU_UBB_BACK_TEMP;
	case SYSTEM_TEMP_UBB_OAM7:
		return AMDGPU_UBB_OAM7_TEMP;
	case SYSTEM_TEMP_UBB_IBC:
		return AMDGPU_UBB_IBC_TEMP;
	case SYSTEM_TEMP_UBB_UFPGA:
		return AMDGPU_UBB_UFPGA_TEMP;
	case SYSTEM_TEMP_UBB_OAM1:
		return AMDGPU_UBB_OAM1_TEMP;
	case SYSTEM_TEMP_OAM_0_1_HSC:
		return AMDGPU_OAM_0_1_HSC_TEMP;
	case SYSTEM_TEMP_OAM_2_3_HSC:
		return AMDGPU_OAM_2_3_HSC_TEMP;
	case SYSTEM_TEMP_OAM_4_5_HSC:
		return AMDGPU_OAM_4_5_HSC_TEMP;
	case SYSTEM_TEMP_OAM_6_7_HSC:
		return AMDGPU_OAM_6_7_HSC_TEMP;
	case SYSTEM_TEMP_UBB_FPGA_0V72_VR:
		return AMDGPU_UBB_FPGA_0V72_VR_TEMP;
	case SYSTEM_TEMP_UBB_FPGA_3V3_VR:
		return AMDGPU_UBB_FPGA_3V3_VR_TEMP;
	case SYSTEM_TEMP_RETIMER_0_1_2_3_1V2_VR:
		return AMDGPU_RETIMER_0_1_2_3_1V2_VR_TEMP;
	case SYSTEM_TEMP_RETIMER_4_5_6_7_1V2_VR:
		return AMDGPU_RETIMER_4_5_6_7_1V2_VR_TEMP;
	case SYSTEM_TEMP_RETIMER_0_1_0V9_VR:
		return AMDGPU_RETIMER_0_1_0V9_VR_TEMP;
	case SYSTEM_TEMP_RETIMER_4_5_0V9_VR:
		return AMDGPU_RETIMER_4_5_0V9_VR_TEMP;
	case SYSTEM_TEMP_RETIMER_2_3_0V9_VR:
		return AMDGPU_RETIMER_2_3_0V9_VR_TEMP;
	case SYSTEM_TEMP_RETIMER_6_7_0V9_VR:
		return AMDGPU_RETIMER_6_7_0V9_VR_TEMP;
	case SYSTEM_TEMP_OAM_0_1_2_3_3V3_VR:
		return AMDGPU_OAM_0_1_2_3_3V3_VR_TEMP;
	case SYSTEM_TEMP_OAM_4_5_6_7_3V3_VR:
		return AMDGPU_OAM_4_5_6_7_3V3_VR_TEMP;
	case SYSTEM_TEMP_IBC_HSC:
		return AMDGPU_IBC_HSC_TEMP;
	case SYSTEM_TEMP_IBC:
		return AMDGPU_IBC_TEMP;
	default:
		return -EINVAL;
	}
}

static bool smu_v13_0_12_is_temp_metrics_supported(struct smu_context *smu,
						   enum smu_temp_metric_type type)
{
	switch (type) {
	case SMU_TEMP_METRIC_BASEBOARD:
		if (smu->adev->gmc.xgmi.physical_node_id == 0 &&
		    smu->adev->gmc.xgmi.num_physical_nodes > 1 &&
		    smu_v13_0_6_cap_supported(smu, SMU_CAP(TEMP_METRICS)))
			return true;
		break;
	case SMU_TEMP_METRIC_GPUBOARD:
		return smu_v13_0_6_cap_supported(smu, SMU_CAP(TEMP_METRICS));
	default:
		break;
	}

	return false;
}

int smu_v13_0_12_get_npm_data(struct smu_context *smu,
			      enum amd_pp_sensors sensor,
			      uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct PPTable_t *pptable =
		(struct PPTable_t *)smu_table->driver_pptable;
	struct smu_table *tables = smu_table->tables;
	SystemMetricsTable_t *metrics;
	struct smu_table *sys_table;
	int ret;

	if (!smu_v13_0_6_cap_supported(smu, SMU_CAP(NPM_METRICS)))
		return -EOPNOTSUPP;

	if (sensor == AMDGPU_PP_SENSOR_MAXNODEPOWERLIMIT) {
		*value = pptable->MaxNodePowerLimit;
		return 0;
	}

	ret = smu_v13_0_12_get_system_metrics_table(smu);
	if (ret)
		return ret;

	sys_table = &tables[SMU_TABLE_PMFW_SYSTEM_METRICS];
	metrics = (SystemMetricsTable_t *)sys_table->cache.buffer;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_NODEPOWERLIMIT:
		*value = SMUQ10_ROUND(metrics->NodePowerLimit);
		break;
	case AMDGPU_PP_SENSOR_NODEPOWER:
		*value = SMUQ10_ROUND(metrics->NodePower);
		break;
	case AMDGPU_PP_SENSOR_GPPTRESIDENCY:
		*value = SMUQ10_ROUND(metrics->GlobalPPTResidencyAcc);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static ssize_t smu_v13_0_12_get_temp_metrics(struct smu_context *smu,
					     enum smu_temp_metric_type type, void *table)
{
	struct amdgpu_baseboard_temp_metrics_v1_0 *baseboard_temp_metrics;
	struct amdgpu_gpuboard_temp_metrics_v1_0 *gpuboard_temp_metrics;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	SystemMetricsTable_t *metrics;
	struct smu_table *data_table;
	struct smu_table *sys_table;
	int ret, sensor_type;
	u32 idx, sensors;
	ssize_t size;

	if (type == SMU_TEMP_METRIC_BASEBOARD) {
		/* Initialize base board temperature metrics */
		data_table =
			&smu->smu_table.tables[SMU_TABLE_BASEBOARD_TEMP_METRICS];
		baseboard_temp_metrics =
			(struct amdgpu_baseboard_temp_metrics_v1_0 *)
				data_table->cache.buffer;
		size = sizeof(*baseboard_temp_metrics);
	} else {
		data_table =
			&smu->smu_table.tables[SMU_TABLE_GPUBOARD_TEMP_METRICS];
		gpuboard_temp_metrics =
			(struct amdgpu_gpuboard_temp_metrics_v1_0 *)
				data_table->cache.buffer;
		size = sizeof(*baseboard_temp_metrics);
	}

	ret = smu_v13_0_12_get_system_metrics_table(smu);
	if (ret)
		return ret;

	sys_table = &tables[SMU_TABLE_PMFW_SYSTEM_METRICS];
	metrics = (SystemMetricsTable_t *)sys_table->cache.buffer;
	smu_table_cache_update_time(data_table, jiffies);

	if (type == SMU_TEMP_METRIC_GPUBOARD) {
		gpuboard_temp_metrics->accumulation_counter = metrics->AccumulationCounter;
		gpuboard_temp_metrics->label_version = metrics->LabelVersion;
		gpuboard_temp_metrics->node_id = metrics->NodeIdentifier;

		idx = 0;
		for (sensors = 0; sensors < NODE_TEMP_MAX_TEMP_ENTRIES; sensors++) {
			if (metrics->NodeTemperatures[sensors] != -1) {
				sensor_type = smu_v13_0_12_get_node_sensor_type(sensors);
				gpuboard_temp_metrics->node_temp[idx] =
					((int)metrics->NodeTemperatures[sensors])  & 0xFFFFFF;
				gpuboard_temp_metrics->node_temp[idx] |= (sensor_type << 24);
				idx++;
			}
		}

		idx = 0;

		for (sensors = 0; sensors < SVI_MAX_TEMP_ENTRIES; sensors++) {
			if (metrics->VrTemperatures[sensors] != -1) {
				sensor_type = smu_v13_0_12_get_vr_sensor_type(sensors);
				gpuboard_temp_metrics->vr_temp[idx] =
					((int)metrics->VrTemperatures[sensors])  & 0xFFFFFF;
				gpuboard_temp_metrics->vr_temp[idx] |= (sensor_type << 24);
				idx++;
			}
		}
	} else if (type == SMU_TEMP_METRIC_BASEBOARD) {
		baseboard_temp_metrics->accumulation_counter = metrics->AccumulationCounter;
		baseboard_temp_metrics->label_version = metrics->LabelVersion;
		baseboard_temp_metrics->node_id = metrics->NodeIdentifier;

		idx = 0;
		for (sensors = 0; sensors < SYSTEM_TEMP_MAX_ENTRIES; sensors++) {
			if (metrics->SystemTemperatures[sensors] != -1) {
				sensor_type = smu_v13_0_12_get_system_sensor_type(sensors);
				baseboard_temp_metrics->system_temp[idx] =
					((int)metrics->SystemTemperatures[sensors])  & 0xFFFFFF;
				baseboard_temp_metrics->system_temp[idx] |= (sensor_type << 24);
				idx++;
			}
		}
	}

	memcpy(table, data_table->cache.buffer, size);

	return size;
}

ssize_t smu_v13_0_12_get_xcp_metrics(struct smu_context *smu, struct amdgpu_xcp *xcp, void *table, void *smu_metrics)
{
	const u8 num_jpeg_rings = NUM_JPEG_RINGS_FW;
	struct amdgpu_partition_metrics_v1_0 *xcp_metrics;
	struct amdgpu_device *adev = smu->adev;
	MetricsTable_t *metrics;
	int inst, j, k, idx;
	u32 inst_mask;

	metrics = (MetricsTable_t *)smu_metrics;
	xcp_metrics = (struct amdgpu_partition_metrics_v1_0 *) table;
	smu_cmn_init_partition_metrics(xcp_metrics, 1, 0);
	amdgpu_xcp_get_inst_details(xcp, AMDGPU_XCP_VCN, &inst_mask);
	idx = 0;
	for_each_inst(k, inst_mask) {
		/* Both JPEG and VCN has same instance */
		inst = GET_INST(VCN, k);
		for (j = 0; j < num_jpeg_rings; ++j) {
			xcp_metrics->jpeg_busy[(idx * num_jpeg_rings) + j] =
				SMUQ10_ROUND(metrics->
					JpegBusy[(inst * num_jpeg_rings) + j]);
		}
		xcp_metrics->vcn_busy[idx] =
			SMUQ10_ROUND(metrics->VcnBusy[inst]);
		xcp_metrics->current_vclk0[idx] = SMUQ10_ROUND(
			metrics->VclkFrequency[inst]);
		xcp_metrics->current_dclk0[idx] = SMUQ10_ROUND(
			metrics->DclkFrequency[inst]);
		xcp_metrics->current_socclk[idx] = SMUQ10_ROUND(
			metrics->SocclkFrequency[inst]);

		idx++;
	}

	xcp_metrics->current_uclk =
		SMUQ10_ROUND(metrics->UclkFrequency);

	amdgpu_xcp_get_inst_details(xcp, AMDGPU_XCP_GFX, &inst_mask);
	idx = 0;
	for_each_inst(k, inst_mask) {
		inst = GET_INST(GC, k);
		xcp_metrics->current_gfxclk[idx] = SMUQ10_ROUND(metrics->GfxclkFrequency[inst]);
		xcp_metrics->gfx_busy_inst[idx] = SMUQ10_ROUND(metrics->GfxBusy[inst]);
		xcp_metrics->gfx_busy_acc[idx] = SMUQ10_ROUND(metrics->GfxBusyAcc[inst]);
		if (smu_v13_0_6_cap_supported(smu, SMU_CAP(HST_LIMIT_METRICS))) {
			xcp_metrics->gfx_below_host_limit_ppt_acc[idx] = SMUQ10_ROUND(metrics->GfxclkBelowHostLimitPptAcc[inst]);
			xcp_metrics->gfx_below_host_limit_thm_acc[idx] = SMUQ10_ROUND(metrics->GfxclkBelowHostLimitThmAcc[inst]);
			xcp_metrics->gfx_low_utilization_acc[idx] = SMUQ10_ROUND(metrics->GfxclkLowUtilizationAcc[inst]);
			xcp_metrics->gfx_below_host_limit_total_acc[idx] = SMUQ10_ROUND(metrics->GfxclkBelowHostLimitTotalAcc[inst]);
		}
		idx++;
	}

	return sizeof(*xcp_metrics);
}

ssize_t smu_v13_0_12_get_gpu_metrics(struct smu_context *smu, void **table, void *smu_metrics)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct gpu_metrics_v1_8 *gpu_metrics =
		(struct gpu_metrics_v1_8 *)smu_table->gpu_metrics_table;
	int ret = 0, xcc_id, inst, i, j, k, idx;
	struct amdgpu_device *adev = smu->adev;
	u8 num_jpeg_rings_gpu_metrics;
	MetricsTable_t *metrics;
	struct amdgpu_xcp *xcp;
	u32 inst_mask;

	metrics = (MetricsTable_t *)smu_metrics;

	smu_cmn_init_soft_gpu_metrics(gpu_metrics, 1, 8);

	gpu_metrics->temperature_hotspot =
		SMUQ10_ROUND(metrics->MaxSocketTemperature);
	/* Individual HBM stack temperature is not reported */
	gpu_metrics->temperature_mem =
		SMUQ10_ROUND(metrics->MaxHbmTemperature);
	/* Reports max temperature of all voltage rails */
	gpu_metrics->temperature_vrsoc =
		SMUQ10_ROUND(metrics->MaxVrTemperature);

	gpu_metrics->average_gfx_activity =
		SMUQ10_ROUND(metrics->SocketGfxBusy);
	gpu_metrics->average_umc_activity =
		SMUQ10_ROUND(metrics->DramBandwidthUtilization);

	gpu_metrics->mem_max_bandwidth =
		SMUQ10_ROUND(metrics->MaxDramBandwidth);

	gpu_metrics->curr_socket_power =
		SMUQ10_ROUND(metrics->SocketPower);
	/* Energy counter reported in 15.259uJ (2^-16) units */
	gpu_metrics->energy_accumulator = metrics->SocketEnergyAcc;

	for (i = 0; i < MAX_GFX_CLKS; i++) {
		xcc_id = GET_INST(GC, i);
		if (xcc_id >= 0)
			gpu_metrics->current_gfxclk[i] =
				SMUQ10_ROUND(metrics->GfxclkFrequency[xcc_id]);

		if (i < MAX_CLKS) {
			gpu_metrics->current_socclk[i] =
				SMUQ10_ROUND(metrics->SocclkFrequency[i]);
			inst = GET_INST(VCN, i);
			if (inst >= 0) {
				gpu_metrics->current_vclk0[i] =
					SMUQ10_ROUND(metrics->VclkFrequency[inst]);
				gpu_metrics->current_dclk0[i] =
					SMUQ10_ROUND(metrics->DclkFrequency[inst]);
			}
		}
	}

	gpu_metrics->current_uclk = SMUQ10_ROUND(metrics->UclkFrequency);

	/* Total accumulated cycle counter */
	gpu_metrics->accumulation_counter = metrics->AccumulationCounter;

	/* Accumulated throttler residencies */
	gpu_metrics->prochot_residency_acc = metrics->ProchotResidencyAcc;
	gpu_metrics->ppt_residency_acc = metrics->PptResidencyAcc;
	gpu_metrics->socket_thm_residency_acc = metrics->SocketThmResidencyAcc;
	gpu_metrics->vr_thm_residency_acc = metrics->VrThmResidencyAcc;
	gpu_metrics->hbm_thm_residency_acc = metrics->HbmThmResidencyAcc;

	/* Clock Lock Status. Each bit corresponds to each GFXCLK instance */
	gpu_metrics->gfxclk_lock_status = metrics->GfxLockXCDMak >> GET_INST(GC, 0);

	gpu_metrics->pcie_link_width = metrics->PCIeLinkWidth;
	gpu_metrics->pcie_link_speed =
		pcie_gen_to_speed(metrics->PCIeLinkSpeed);
	gpu_metrics->pcie_bandwidth_acc =
		SMUQ10_ROUND(metrics->PcieBandwidthAcc[0]);
	gpu_metrics->pcie_bandwidth_inst =
		SMUQ10_ROUND(metrics->PcieBandwidth[0]);
	gpu_metrics->pcie_l0_to_recov_count_acc = metrics->PCIeL0ToRecoveryCountAcc;
	gpu_metrics->pcie_replay_count_acc = metrics->PCIenReplayAAcc;
	gpu_metrics->pcie_replay_rover_count_acc =
		metrics->PCIenReplayARolloverCountAcc;
	gpu_metrics->pcie_nak_sent_count_acc = metrics->PCIeNAKSentCountAcc;
	gpu_metrics->pcie_nak_rcvd_count_acc = metrics->PCIeNAKReceivedCountAcc;
	gpu_metrics->pcie_lc_perf_other_end_recovery = metrics->PCIeOtherEndRecoveryAcc;

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	gpu_metrics->gfx_activity_acc = SMUQ10_ROUND(metrics->SocketGfxBusyAcc);
	gpu_metrics->mem_activity_acc = SMUQ10_ROUND(metrics->DramBandwidthUtilizationAcc);

	for (i = 0; i < NUM_XGMI_LINKS; i++) {
		j = amdgpu_xgmi_get_ext_link(adev, i);
		if (j < 0 || j >= NUM_XGMI_LINKS)
			continue;
		gpu_metrics->xgmi_read_data_acc[j] =
			SMUQ10_ROUND(metrics->XgmiReadDataSizeAcc[i]);
		gpu_metrics->xgmi_write_data_acc[j] =
			SMUQ10_ROUND(metrics->XgmiWriteDataSizeAcc[i]);
		ret = amdgpu_get_xgmi_link_status(adev, i);
		if (ret >= 0)
			gpu_metrics->xgmi_link_status[j] = ret;
	}

	gpu_metrics->num_partition = adev->xcp_mgr->num_xcps;

	num_jpeg_rings_gpu_metrics = NUM_JPEG_RINGS_GPU_METRICS(gpu_metrics);
	for_each_xcp(adev->xcp_mgr, xcp, i) {
		amdgpu_xcp_get_inst_details(xcp, AMDGPU_XCP_VCN, &inst_mask);
		idx = 0;
		for_each_inst(k, inst_mask) {
			/* Both JPEG and VCN has same instances */
			inst = GET_INST(VCN, k);

			for (j = 0; j < num_jpeg_rings_gpu_metrics; ++j) {
				gpu_metrics->xcp_stats[i].jpeg_busy
					[(idx * num_jpeg_rings_gpu_metrics) + j] =
					SMUQ10_ROUND(metrics->JpegBusy
							[(inst * NUM_JPEG_RINGS_FW) + j]);
			}
			gpu_metrics->xcp_stats[i].vcn_busy[idx] =
			       SMUQ10_ROUND(metrics->VcnBusy[inst]);
			idx++;
		}

		amdgpu_xcp_get_inst_details(xcp, AMDGPU_XCP_GFX, &inst_mask);
		idx = 0;
		for_each_inst(k, inst_mask) {
			inst = GET_INST(GC, k);
			gpu_metrics->xcp_stats[i].gfx_busy_inst[idx] =
				SMUQ10_ROUND(metrics->GfxBusy[inst]);
			gpu_metrics->xcp_stats[i].gfx_busy_acc[idx] =
				SMUQ10_ROUND(metrics->GfxBusyAcc[inst]);
			if (smu_v13_0_6_cap_supported(smu, SMU_CAP(HST_LIMIT_METRICS))) {
				gpu_metrics->xcp_stats[i].gfx_below_host_limit_ppt_acc[idx] =
					SMUQ10_ROUND(metrics->GfxclkBelowHostLimitPptAcc[inst]);
				gpu_metrics->xcp_stats[i].gfx_below_host_limit_thm_acc[idx] =
					SMUQ10_ROUND(metrics->GfxclkBelowHostLimitThmAcc[inst]);
				gpu_metrics->xcp_stats[i].gfx_low_utilization_acc[idx] =
					SMUQ10_ROUND(metrics->GfxclkLowUtilizationAcc[inst]);
				gpu_metrics->xcp_stats[i].gfx_below_host_limit_total_acc[idx] =
					SMUQ10_ROUND(metrics->GfxclkBelowHostLimitTotalAcc[inst]);
			}
			idx++;
		}
	}

	gpu_metrics->xgmi_link_width = metrics->XgmiWidth;
	gpu_metrics->xgmi_link_speed = metrics->XgmiBitrate;

	gpu_metrics->firmware_timestamp = metrics->Timestamp;

	*table = (void *)gpu_metrics;

	return sizeof(*gpu_metrics);
}

const struct smu_temp_funcs smu_v13_0_12_temp_funcs = {
	.temp_metrics_is_supported = smu_v13_0_12_is_temp_metrics_supported,
	.get_temp_metrics = smu_v13_0_12_get_temp_metrics,
};
